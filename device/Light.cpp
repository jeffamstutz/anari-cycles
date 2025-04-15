// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Light.h"
// cycles
#include "scene/camera.h"

namespace anari_cycles {

// Helper functions ///////////////////////////////////////////////////////////

inline math::mat4 rotationFromZNegativeToTarget(const math::float3 &targetDir)
{
  const math::float3 from = {0.0f, 0.0f, -1.0f};
  math::float3 to = math::normalize(targetDir);

  float cosTheta = math::dot(from, to);

  // If the directions are nearly the same
  if (std::abs(cosTheta - 1.0f) < 1e-6f)
    return linalg::identity;

  // If the directions are opposite
  if (std::abs(cosTheta + 1.0f) < 1e-6f) {
    // Find an arbitrary perpendicular axis to rotate 180° around
    math::float3 axis = math::cross(from, math::float3{1.0f, 0.0f, 0.0f});
    if (math::length(axis) < 1e-6f)
      axis = math::cross(from, math::float3{0.0f, 1.0f, 0.0f});
    axis = math::normalize(axis);

    float x = axis.x, y = axis.y, z = axis.z;
    return math::mat4{{1 - 2 * y * y - 2 * z * z, 2 * x * y, 2 * x * z, 0.f},
        {2 * x * y, 1 - 2 * x * x - 2 * z * z, 2 * y * z, 0.f},
        {2 * x * z, 2 * y * z, 1 - 2 * x * x - 2 * y * y, 0.f},
        {0.f, 0.f, 0.f, 1.f}};
  }

  // Otherwise, use Rodrigues' rotation formula
  math::float3 axis = math::normalize(math::cross(from, to));
  float s = std::sqrt(1.0f - cosTheta * cosTheta);
  math::mat3 K = {{0.0f, -axis.z, axis.y},
      {axis.z, 0.0f, -axis.x},
      {-axis.y, axis.x, 0.0f}};

  auto result = math::mat3{linalg::identity} + K * s
      + mul(K, K) * ((1.0f - cosTheta) / (s * s));

  return math::mat4{{result[0].x, result[0].y, result[0].z, 0.0f},
      {result[1].x, result[1].y, result[1].z, 0.0f},
      {result[2].x, result[2].y, result[2].z, 0.0f},
      {0.0f, 0.0f, 0.0f, 1.0f}};
}

// Subtype declarations ///////////////////////////////////////////////////////

struct Directional : public Light
{
  Directional(CyclesGlobalState *s);

  void commitParameters() override;
  void finalize() override;
  math::mat4 xfm() const override;

 private:
  math::float3 m_direction{0.f, 0.f, -1.f};
  math::float3 m_prevDirection{0.f, 0.f, 0.f};
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
  m_direction =
      math::normalize(getParam<math::float3>("direction", {0.f, 0.f, -1.f}));
  m_irradiance = std::clamp(getParam<float>("irradiance", 1.f),
      0.f,
      std::numeric_limits<float>::max());
}

void Directional::finalize()
{
  m_cyclesLight->set_light_type(LIGHT_DISTANT);

  if (m_prevDirection != m_direction) {
    reportMessage(ANARI_SEVERITY_WARNING, "make light updates more efficient!");
    deviceState()->objectUpdates.lastSceneChange = helium::newTimeStamp();
    m_prevDirection = m_direction;
  }

  m_cyclesLight->set_strength(
      m_irradiance * ccl::make_float3(m_color[0], m_color[1], m_color[2]));
  m_cyclesLight->tag_update(deviceState()->scene);
}

math::mat4 Directional::xfm() const
{
  return math::inverse(rotationFromZNegativeToTarget(m_direction));
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Light *);
