// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Light.h"
// cycles
#include "scene/camera.h"

namespace cycles {

// Subtype declarations ///////////////////////////////////////////////////////

struct Directional : public Light
{
  Directional(CyclesGlobalState *s);

  void commitParameters() override;
  void finalize() override;

 private:
  anari_vec::float3 m_direction{0.f, 0.f, -1.f};
  float m_irradiance{1.f};
};

// Light definitions //////////////////////////////////////////////////////////

Light::Light(CyclesGlobalState *s) : Object(ANARI_LIGHT, s)
{
  m_cyclesLight = s->scene->create_node<ccl::Light>();
}

Light::~Light()
{
  reportMessage(ANARI_SEVERITY_WARNING, "TODO: cleanup Cycles light objects");
}

Light *Light::createInstance(std::string_view type, CyclesGlobalState *s)
{
  if (type == "directional")
    return new Directional(s);
  else
    return (Light *)new UnknownObject(ANARI_LIGHT, type, s);
}

void Light::commitParameters()
{
  m_color = getParam<anari_vec::float3>("color", {1.f, 1.f, 1.f});
}

ccl::Light *Light::cyclesLight() const
{
  return m_cyclesLight;
}

// Directional definitions ////////////////////////////////////////////////////

Directional::Directional(CyclesGlobalState *s) : Light(s) {}

void Directional::commitParameters()
{
  Light::commitParameters();

  m_direction = getParam<anari_vec::float3>("direction", {0.f, 0.f, -1.f});
  m_irradiance = std::clamp(getParam<float>("irradiance", 1.f),
      0.f,
      std::numeric_limits<float>::max());
}

void Directional::finalize()
{
  m_cyclesLight->set_light_type(LIGHT_DISTANT);

  auto direction = normalize(
      ccl::make_float3(m_direction[0], m_direction[1], m_direction[2]));
  auto color = ccl::make_float3(m_color[0], m_color[1], m_color[2]);

  m_cyclesLight->set_strength(m_irradiance * color);

#if 0
  auto tfm = m_cyclesLight->get_tfm();
  transform_set_column(&tfm, 2, -direction);
  m_cyclesLight->set_tfm(tfm);
#endif

  m_cyclesLight->tag_update(deviceState()->scene);
}

} // namespace cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(cycles::Light *);
