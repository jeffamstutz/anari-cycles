// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Directional.h"
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
#include <cmath>

namespace anari_cycles {

Directional::Directional(CyclesGlobalState *s)
    : Light(s, s->scene->create_node<ccl::SunLight>())
{
  attachUnitEmissionShader();
}

void Directional::commitParameters()
{
  Light::commitParameters();
  m_direction = getNormalizedDirection("direction", {0.f, 0.f, -1.f});
  // KHR_AREA_LIGHTS 'angularDiameter': apparent (full) angle of the sun
  // disc, matching the Cycles SunLight 'angle' socket (also a full angle;
  // the kernel halves it itself).
  m_angularDiameter = std::clamp(
      getParam<float>("angularDiameter", 0.f), 0.f, float(M_PI));
  // KHR_AREA_LIGHTS 'radiance' (surface radiance of the sun disc) takes
  // precedence over the base 'irradiance'.
  m_usesRadiance = hasParam("radiance", ANARI_FLOAT32);
  m_strengthValue = std::clamp(m_usesRadiance
          ? getParam<float>("radiance", 1.f)
          : getParam<float>("irradiance", 1.f),
      0.f,
      std::numeric_limits<float>::max());
}

void Directional::finalize()
{
  auto *light = static_cast<ccl::SunLight *>(m_cyclesLight);
  light->set_angle(m_angularDiameter);
  if (m_usesRadiance) {
    if (m_angularDiameter <= 0.f) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "directional light 'radiance' with angularDiameter 0 is a "
          "degenerate (delta) sun; treating the value as irradiance");
    }
    // With normalize off, Cycles emits 'strength' directly as the disc's
    // radiance (SunLight eval_fac = 1). The resulting irradiance on a
    // surface facing the light is radiance * pi * sin^2(angularDiameter/2);
    // at angularDiameter 0 the kernel degenerates to a delta light whose
    // irradiance equals 'strength'.
    light->set_normalize(false);
  } else {
    // Cycles' normalized sun divides by the disc's solid angle
    // (area() = pi * sin^2(angle/2)), making 'strength' the irradiance on
    // a surface facing the light independent of angularDiameter -- exactly
    // ANARI 'irradiance'.
    light->set_normalize(true);
  }
  m_cyclesLight->set_strength(scaledColor(m_strengthValue));
  m_cyclesLight->tag_update(deviceState()->scene);

  Light::finalize();
}

math::mat4 Directional::xfm() const
{
  return math::inverse(rotationFromZNegativeToTarget(m_direction));
}

} // namespace anari_cycles
