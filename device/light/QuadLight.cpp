// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "QuadLight.h"
// cycles
#include "kernel/svm/types.h"
#include "scene/background.h"
#include "scene/camera.h"
#include "scene/shader.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "util/math_base.h"
#include "util/transform.h"
#include "util/types_float3.h"
// std
#include <cmath>

namespace anari_cycles {

QuadLight::QuadLight(CyclesGlobalState *s)
    : Light(s, s->scene->create_node<ccl::AreaLight>())
{
  attachUnitEmissionShader();
  m_cyclesLightBack = s->scene->create_node<ccl::AreaLight>();
  ccl::array<ccl::Node *> usedShaders;
  usedShaders.push_back_slow(m_cyclesShader);
  m_cyclesLightBack->set_used_shaders(usedShaders);
  m_cyclesLightBack->set_use_mis(true); // see attachUnitEmissionShader()
}

QuadLight::~QuadLight()
{
  // Same deferred deletion as the primary light in ~Light().
  CyclesGlobalState::SceneLock sceneLock(*deviceState());
  deviceState()->retireGeometry(m_cyclesLightBack);
}

bool QuadLight::isValid() const
{
  return m_area > 0.f;
}

void QuadLight::commitParameters()
{
  Light::commitParameters();
  m_position = getParam<math::float3>("position", {0.f, 0.f, 0.f});
  m_edge1 = getParam<math::float3>("edge1", {1.f, 0.f, 0.f});
  m_edge2 = getParam<math::float3>("edge2", {0.f, 1.f, 0.f});
  m_area = math::length(math::cross(m_edge1, m_edge2));
  if (!std::isfinite(m_area))
    m_area = 0.f;
  m_radiance = photometricRadiance(m_area);
  m_side = getParamString("side", "front");
}

void QuadLight::finalize()
{
  // Re-read the distribution *contents* here: a change committed on the
  // array itself (new data or region) re-runs finalize() only.
  m_distribution = getIntensityDistributionParam();

  if (m_area <= 0.f) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "quad light 'edge1'/'edge2' span zero area (degenerate or "
        "non-finite); skipping light");
    Light::finalize();
    return;
  }
  if (m_side != "front" && m_side != "back" && m_side != "both") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "invalid quad light side '%s'; using side='front'",
        m_side.c_str());
    m_side = "front";
  }

  // side='both' instances the back emitter too (see secondaryCyclesLight());
  // each face then emits the resolved radiance, so 'intensity'/'power' are
  // interpreted per face (total flux doubles).
  const auto setup = [&](ccl::Light *cyclesLight) {
    auto *light = static_cast<ccl::AreaLight *>(cyclesLight);
    light->set_sizeu(1.f);
    light->set_sizev(1.f);
    light->set_ellipse(false);
    light->set_spread(float(M_PI));
    // With normalize off, the emitted radiance is strength/pi independent of
    // the light's area (kernel eval_fac = invarea/pi with invarea = 1), so
    // ANARI 'radiance' maps to strength = pi * radiance. This also keeps the
    // radiance invariant under instance scaling.
    light->set_normalize(false);
    light->set_strength(scaledColor(float(M_PI) * m_radiance));
    light->tag_update(deviceState()->scene);
  };
  setup(m_cyclesLight);
  if (m_side == "both") // the back emitter is only instanced for 'both'
    setup(m_cyclesLightBack);

  // intensityDistribution: gamma is measured from the emitting face's
  // normal, the C0 half-plane is anchored at edge1 (KHR_LIGHT_QUAD). The
  // matrix rows below map the local emission direction -- whose coordinates
  // are in the (possibly skewed/scaled) edge1/edge2/normal basis baked into
  // quadXfm() -- to the orthonormal-frame vector the IES kernel expects
  // (see Light.h). For side='back'/'both' the flipped per-face transform
  // mirrors the profile about the quad's plane automatically.
  {
    auto n = math::cross(m_edge1, m_edge2);
    const float nLen = math::length(n);
    n = nLen > 0.f ? n / nLen : math::float3{0.f, 0.f, 1.f};
    const float e1Len = math::length(m_edge1);
    const auto e1Hat =
        e1Len > 0.f ? m_edge1 / e1Len : math::float3{1.f, 0.f, 0.f};
    const auto e2Hat = math::cross(n, e1Hat);
    updateEmissionShaderDistribution(m_distribution,
        {0.f, -math::dot(m_edge2, e2Hat), 0.f},
        {-e1Len, -math::dot(m_edge2, e1Hat), 0.f},
        {0.f, 0.f, 1.f});
  }
  Light::finalize();
}

ccl::Light *QuadLight::secondaryCyclesLight() const
{
  return m_side == "both" ? m_cyclesLightBack : nullptr;
}

math::mat4 QuadLight::xfm() const
{
  return quadXfm(m_side == "back");
}

math::mat4 QuadLight::secondaryXfm() const
{
  return quadXfm(true);
}

math::mat4 QuadLight::quadXfm(bool backSide) const
{
  const auto center = m_position + 0.5f * (m_edge1 + m_edge2);

  auto normal = math::cross(m_edge1, m_edge2);
  if (math::length(normal) > 0.f)
    normal = math::normalize(normal);
  else
    normal = {0.f, 0.f, 1.f};

  if (backSide)
    normal = -normal;

  return math::mat4{{m_edge1.x, m_edge1.y, m_edge1.z, 0.f},
      {m_edge2.x, m_edge2.y, m_edge2.z, 0.f},
      {-normal.x, -normal.y, -normal.z, 0.f},
      {center.x, center.y, center.z, 1.f}};
}

} // namespace anari_cycles
