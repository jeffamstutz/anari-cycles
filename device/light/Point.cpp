// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Point.h"
#include "LightUtils.h"
// std
#include <algorithm>
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

Point::Point(CyclesGlobalState *s)
    : Light(s, s->scene->create_node<ccl::PointLight>())
{
  attachUnitEmissionShader();
}

void Point::commitParameters()
{
  Light::commitParameters();
  m_position = getParam<math::float3>("position", {0.f, 0.f, 0.f});
  m_radius = std::max(getParam<float>("radius", 0.f), 0.f);
  // Photometric precedence: 'radiance' (KHR_AREA_LIGHTS, sphere surface
  // radiance) over 'intensity' (W/sr) over 'power' (W).
  float value = 1.f;
  if (hasParam("radiance", ANARI_FLOAT32)) {
    m_quantity = Quantity::RADIANCE;
    value = getParam<float>("radiance", 1.f);
  } else if (hasParam("intensity", ANARI_FLOAT32)
      || !hasParam("power", ANARI_FLOAT32)) {
    m_quantity = Quantity::INTENSITY;
    value = getParam<float>("intensity", 1.f);
  } else {
    m_quantity = Quantity::POWER;
    value = getParam<float>("power", 1.f);
  }
  m_value = std::clamp(value, 0.f, std::numeric_limits<float>::max());
}

void Point::finalize()
{
  auto *light = static_cast<ccl::PointLight *>(m_cyclesLight);
  light->set_radius(m_radius);
  switch (m_quantity) {
  case Quantity::RADIANCE:
    if (m_radius <= 0.f) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "point light 'radiance' with radius 0 is a degenerate sphere; "
          "treating the value as radiant intensity (W/sr)");
    }
    // With normalize off the kernel's eval_fac is 1/pi independent of the
    // radius, so the sphere's surface radiance is strength/pi.
    light->set_normalize(false);
    m_cyclesLight->set_strength(scaledColor(float(M_PI) * m_value));
    break;
  case Quantity::INTENSITY:
    // ANARI 'intensity' is radiant intensity (W/sr); Cycles interprets
    // strength as total radiant flux (W) when 'normalize' is on, so the
    // isotropic conversion is flux = 4*pi * intensity.
    light->set_normalize(true);
    m_cyclesLight->set_strength(scaledColor(4.f * float(M_PI) * m_value));
    break;
  case Quantity::POWER:
    // ANARI 'power' is the total radiant flux (W) -- exactly Cycles'
    // normalized strength.
    light->set_normalize(true);
    m_cyclesLight->set_strength(scaledColor(m_value));
    break;
  }
  m_cyclesLight->tag_update(deviceState()->scene);
  Light::finalize();
}

math::mat4 Point::xfm() const
{
  auto m = math::mat4(linalg::identity);
  m[3] = {m_position.x, m_position.y, m_position.z, 1.f};
  return m;
}

} // namespace anari_cycles
