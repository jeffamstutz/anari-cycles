// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Ring.h"
#include "LightUtils.h"
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
#include <algorithm>
#include <cmath>

namespace anari_cycles {

// Stand-in radius for degenerate (zero-radius) ring lights.
static constexpr float g_minRingRadius = 1e-3f;

Ring::Ring(CyclesGlobalState *s)
    : Light(s, s->scene->create_node<ccl::AreaLight>())
{
  attachUnitEmissionShader();
}

void Ring::commitParameters()
{
  Light::commitParameters();
  m_position = getParam<math::float3>("position", {0.f, 0.f, 0.f});
  m_direction = getNormalizedDirection("direction", {0.f, 0.f, -1.f});
  // ANARI 'openingAngle' is the full cone angle of emission (default pi =
  // full hemisphere), matching the Cycles area-light 'spread' socket (also
  // a full angle with default pi).
  m_openingAngle =
      std::clamp(getParam<float>("openingAngle", float(M_PI)), 0.f, float(M_PI));
  m_falloffAngleSet = hasParam("falloffAngle", ANARI_FLOAT32);
  m_radius = std::max(getParam<float>("radius", 0.f), 0.f);
  m_innerRadius = std::max(getParam<float>("innerRadius", 0.f), 0.f);

  // The registry default radius is 0, a degenerate disc Cycles cannot
  // sample; substitute a tiny disc so the light still emits and the
  // intensity/power conversions stay well defined. Any positive radius is
  // used as-is.
  m_effectiveRadius = m_radius > 0.f ? m_radius : g_minRingRadius;
  m_radiance =
      photometricRadiance(float(M_PI) * m_effectiveRadius * m_effectiveRadius);
  m_c0 = getParam<math::float3>("c0", {1.f, 0.f, 0.f});
}

void Ring::finalize()
{
  // Re-read the distribution *contents* here: a change committed on the
  // array itself (new data or region) re-runs finalize() only.
  m_distribution = getIntensityDistributionParam();

  if (m_radius <= 0.f) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "ring light radius is 0; using a tiny disc (radius %g) instead",
        double(g_minRingRadius));
  }
  if (m_innerRadius > 0.f) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "ring light innerRadius is not supported (no Cycles analog); "
        "treating the ring as a full disc");
  }
  if (m_falloffAngleSet) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "ring light falloffAngle is not supported; Cycles applies its own "
        "fixed soft edge to the emission cone");
  }

  auto *light = static_cast<ccl::AreaLight *>(m_cyclesLight);
  light->set_sizeu(2.f * m_effectiveRadius);
  light->set_sizev(2.f * m_effectiveRadius);
  light->set_ellipse(true);
  // Cycles 'spread' models a soft-box grid: emission is limited to the
  // spread cone with a soft (linear-in-tangent) rim, renormalized so total
  // flux is independent of the spread angle. ANARI 'openingAngle' instead
  // masks emission (like the spot light) without renormalizing, so divide
  // out the kernel's on-axis renormalization boost: a point on the light
  // then emits the requested radiance along the axis, falling off linearly
  // in tangent toward the cone edge (soft-edge approximation of the cone;
  // ANARI 'falloffAngle' is not otherwise representable).
  light->set_spread(m_openingAngle);
  float spreadCompensation = 1.f;
  const float halfSpread = 0.5f * m_openingAngle;
  if (m_openingAngle <= 0.f) {
    // Kernel emits a delta beam scaled by pi when the spread is zero.
    spreadCompensation = 1.f / float(M_PI);
  } else if (m_openingAngle < float(M_PI)) {
    // Inverse of the kernel's on-axis attenuation tan(half)*normalize_spread
    // (scene/light.cpp AreaLight::copy_to_kernel + kernel/light/area.h
    // area_light_spread_attenuation), including its small-angle branch.
    const float tanHalf = std::tan(halfSpread);
    spreadCompensation = halfSpread > 0.05f
        ? (tanHalf - halfSpread) / tanHalf
        : (halfSpread * halfSpread * halfSpread) / (3.f * tanHalf);
  }
  // Same normalize-off radiance mapping as QuadLight: emitted radiance is
  // strength/pi, so ANARI 'radiance' maps to strength = pi * radiance.
  light->set_normalize(false);
  m_cyclesLight->set_strength(
      scaledColor(float(M_PI) * m_radiance * spreadCompensation));
  m_cyclesLight->tag_update(deviceState()->scene);

  // intensityDistribution: gamma is measured from 'direction'; the C0
  // half-plane is anchored at 'c0' (KHR_LIGHT_RING). The ring's local frame
  // (positionDirectionXfm) is orthonormal with the emission axis on -Z, so
  // the rows only rotate the C origin onto c0 (see Light.h).
  {
    auto c0 = math::float3{1.f, 0.f, 0.f};
    if (m_distribution.present() && m_distribution.nC > 1) {
      // c0 in the light's local frame, projected onto the ring's plane
      const auto worldToLocal = rotationFromZNegativeToTarget(m_direction);
      const auto c0l = math::mul(worldToLocal,
          math::float4{m_c0.x, m_c0.y, m_c0.z, 0.f});
      const float projLen = std::hypot(c0l.x, c0l.y);
      if (std::isfinite(projLen) && projLen > 1e-6f) {
        c0 = math::float3{c0l.x / projLen, c0l.y / projLen, 0.f};
      } else {
        reportMessage(ANARI_SEVERITY_WARNING,
            "ring light 'c0' is parallel to 'direction' (or degenerate); "
            "using an arbitrary C0-plane orientation");
      }
    }
    updateEmissionShaderDistribution(m_distribution,
        {-c0.y, c0.x, 0.f},
        {-c0.x, -c0.y, 0.f},
        {0.f, 0.f, 1.f});
  }
  Light::finalize();
}

math::mat4 Ring::xfm() const
{
  return positionDirectionXfm(m_position, m_direction);
}

} // namespace anari_cycles
