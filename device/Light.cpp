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

// Light definitions /////////////////////////////////////////////////////////

Light::Light(CyclesGlobalState *s) : Object(ANARI_CAMERA, s) {}

Light::~Light() = default;

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

std::unique_ptr<ccl::Light> Light::cyclesLight()
{
  auto cl = std::make_unique<ccl::Light>();

  cl->set_shader(deviceState()->scene->default_light);
  cl->set_use_camera(true);
  cl->set_use_diffuse(true);
  cl->set_use_glossy(true);
  cl->set_use_transmission(true);
  cl->set_use_scatter(true);
  cl->set_cast_shadow(true);

  m_cyclesLight = cl.get();
  this->finalize();
  m_cyclesLight = nullptr;

  return cl;
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
  if (!m_cyclesLight)
    return;

  m_cyclesLight->set_light_type(LIGHT_DISTANT);

  auto direction = normalize(
      ccl::make_float3(m_direction[0], m_direction[1], m_direction[2]));
  auto color = ccl::make_float3(m_color[0], m_color[1], m_color[2]);

  auto &state = *deviceState();
  m_cyclesLight->set_strength(m_irradiance * color);

  auto tfm = m_cyclesLight->get_tfm();
  transform_set_column(&tfm, 2, -direction);
  m_cyclesLight->set_tfm(tfm);
  m_cyclesLight->tag_update(state.scene);
}

} // namespace cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(cycles::Light *);
