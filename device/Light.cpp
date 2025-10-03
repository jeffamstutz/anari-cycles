// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Light.h"
#include <anari/anari_cpp/ext/linalg.h>
#include <zstd_errors.h>
#include <cmath>
#include <cstdio>
#include "Sampler.h"

// cycles
#include "SamplerImageLoader.h"
#include "kernel/svm/types.h"
#include "scene/camera.h"
#include "scene/colorspace.h"
#include "scene/shader.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "scene/background.h"
#include "util/math_base.h"
#include "util/transform.h"
#include "util/types_float3.h"

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

struct HDRI : public Light
{
  HDRI(CyclesGlobalState *s);
  ~HDRI() override;

  void commitParameters() override;
  void finalize() override;
  math::mat4 xfm() const override;

 private:
  helium::IntrusivePtr<Array2D> m_radiance{};
  math::float3 m_up{0.f, 1.f, 0.f};
  math::float3 m_direction{0.f, 0.f, -1.f};

  float m_scale{1.f};
  bool m_visible{true};
};

// Light definitions //////////////////////////////////////////////////////////

Light::Light(CyclesGlobalState *s) : Object(ANARI_LIGHT, s)
{
  m_cyclesLight = s->scene->create_node<ccl::Light>();
}

Light::~Light()
{
  deviceState()->scene->delete_node(m_cyclesLight);
}

Light *Light::createInstance(std::string_view type, CyclesGlobalState *s)
{
  if (type == "directional")
    return new Directional(s);
  else if (type == "hdri")
    return new HDRI(s);
  else
    return (Light *)new UnknownObject(ANARI_LIGHT, type, s);
}

void Light::commitParameters()
{
  m_color = getParam<anari_vec::float3>("color", {1.f, 1.f, 1.f});
}

void Light::finalize()
{
  Object::finalize();
}

ccl::Light *Light::cyclesLight() const
{
  return m_cyclesLight;
}

ccl::Shader *Light::cyclesShader() const
{
  return m_cyclesShader;
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
    reportMessage(ANARI_SEVERITY_PERFORMANCE_WARNING,
        "make light updates more efficient!");
    deviceState()->objectUpdates.lastSceneChange = helium::newTimeStamp();
    m_prevDirection = m_direction;
  }

  m_cyclesLight->set_strength(
      m_irradiance * ccl::make_float3(m_color[0], m_color[1], m_color[2]));
  m_cyclesLight->tag_update(deviceState()->scene);

  Light::finalize();
}

math::mat4 Directional::xfm() const
{
  return math::inverse(rotationFromZNegativeToTarget(m_direction));
}

// HDRI definitions ///////////////////////////////////////////////////////////

HDRI::HDRI(CyclesGlobalState *s) : Light(s) {}

HDRI::~HDRI() = default;

void HDRI::commitParameters()
{
  Light::commitParameters();

  m_radiance = getParamObject<Array2D>("radiance");
  m_scale = getParam<float>("scale", 1.f);
  m_visible = getParam<bool>("visible", true);

  m_up = getParam<math::float3>("up", {0.f, 1.f, 0.f});
  m_direction = getParam<math::float3>("direction", {0.f, 0.f, 1.f});
}

// Transform vector from ANARI coordinate system to Cycles coordinate system
// ANARI: X-forward, Y-up, Z-right (right-handed)
// Cycles: -Y-forward, Z-up, X-right (right-handed)
inline math::float3 anariToCycles(const math::float3 &anari_vec)
{
  return math::float3(-anari_vec.y, anari_vec.x, anari_vec.z);
}

void HDRI::finalize()
{
  Light::finalize();

  // Set light type for identification purposes
  m_cyclesLight->set_light_type(ccl::LIGHT_BACKGROUND);
  m_cyclesLight->tag_update(deviceState()->scene);

  if (m_cyclesShader) {
    m_cyclesShader->dereference();
    deviceState()->scene->delete_node(m_cyclesShader);
    assert(m_cyclesShader->reference_count() == 0);
    m_cyclesShader = nullptr;
  }

  // Create shader for HDRI if radiance is provided
  if (m_radiance) {
    auto graph = std::make_unique<ccl::ShaderGraph>();

    // Build orthonormal basis from direction and up vectors
    // We should ensure that up is not parallel to forward, let save that for later.
    auto forward = math::normalize(m_direction);
    auto up = math::normalize(m_up);
    auto right = math::normalize(math::cross(forward, up));
    up = math::normalize(math::cross(right, forward)); // Ensure orthogonality

    // Create rotation matrix (column-major: each column is a basis vector)
    // Transform from standard basis to our custom orientation
    // math::mat3 rotationMat = {
    //     {forward.x, right.x, up.x},  // First column
    //     {forward.y, right.y, up.y},  // Second column  
    //     {forward.z, right.z, up.z}   // Third column
    // };
    math::mat3 rotationMat = {
      forward, right, up
    };

    // Extract axis-angle representation for Cycles vector rotation node
    // math::float3 axis;
    // float angle;
    // axis_angle_from_matrix(rotationMat, axis, angle);

    auto rotation = math::rotation_quat(rotationMat);
    float angle = qangle(rotation);
    math::float3 axis = qaxis(rotation);

    auto tex_coords = graph->create_node<ccl::TextureCoordinateNode>();

    auto vectorRotate = graph->create_node<ccl::VectorRotateNode>();
    vectorRotate->set_rotate_type(ccl::NODE_VECTOR_ROTATE_TYPE_AXIS);
    vectorRotate->set_angle(angle);
    vectorRotate->set_axis(ccl::make_float3(axis.x, axis.y, axis.z));
    graph->connect(tex_coords->output("Generated"), vectorRotate->input("Vector"));
    
    // Create environment texture node
    auto *env_tex = graph->create_node<ccl::EnvironmentTextureNode>();
    env_tex->set_projection(ccl::NODE_ENVIRONMENT_EQUIRECTANGULAR);
    env_tex->set_colorspace(ccl::u_colorspace_raw);
    env_tex->set_tex_mapping_type(ccl::TextureMapping::VECTOR);
    env_tex->set_tex_mapping_x_mapping(ccl::TextureMapping::X);
    env_tex->set_tex_mapping_y_mapping(ccl::TextureMapping::Y);
    env_tex->set_tex_mapping_z_mapping(ccl::TextureMapping::Z);
    env_tex->set_tex_mapping_scale(ccl::make_float3(1.0f, 1.0f, 1.0f));

    graph->connect(vectorRotate->output("Vector"), env_tex->input("Vector"));

    // Use SamplerImageLoader to get the image handle
    auto loader = std::make_unique<SamplerImageLoader>(m_radiance.ptr);
    ccl::ImageParams params;
    params.alpha_type = IMAGE_ALPHA_AUTO;
    params.interpolation = INTERPOLATION_LINEAR;

    env_tex->handle =
      deviceState()->scene->image_manager->add_image(std::move(loader), params, false);

    // Create output node
    auto *background = graph->create_node<ccl::BackgroundNode>();
    
    // Connect environment texture to background
    graph->connect(env_tex->output("Color"), background->input("Color"));

    graph->connect(background->output("Background"), graph->output()->input("Surface"));

    // Create shader and assign graph
    m_cyclesShader = deviceState()->scene->create_node<ccl::Shader>();
    graph->dump_graph("/tmp/blender.graph.world.dot");
    m_cyclesShader->set_graph(std::move(graph));
    m_cyclesShader->tag_update(deviceState()->scene);
    m_cyclesShader->reference();
  }

  deviceState()->objectUpdates.lastSceneChange = helium::newTimeStamp();
}

math::mat4 HDRI::xfm() const
{
  return math::mat4(1.0f);

}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Light *);
