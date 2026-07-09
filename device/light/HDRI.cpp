// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "HDRI.h"
#include "sampler/Sampler.h"
#include "sampler/SamplerImageLoader.h"
// std
#include <memory>
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

HDRI::HDRI(CyclesGlobalState *s)
    : Light(s, s->scene->create_node<ccl::BackgroundLight>())
{}

HDRI::~HDRI() = default;

void HDRI::commitParameters()
{
  Light::commitParameters();

  m_radiance = getParamObject<Array2D>("radiance");
  m_scale = getParam<float>("scale", 1.f);

  // Only the registry-defined equirectangular layout is supported.
  const auto layout = getParamString("layout", "equirectangular");
  if (layout != "equirectangular") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "hdri light layout '%s' is not supported; "
        "using 'equirectangular'",
        layout.c_str());
  }

  m_up = getNormalizedDirection("up", {0.f, 0.f, 1.f});
  m_direction = getNormalizedDirection("direction", {1.f, 0.f, 0.f});
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

  m_cyclesLight->tag_update(deviceState()->scene);

  if (m_radiance) {
    rebuildEnvironmentShader();
  } else if (m_cyclesShader) {
    m_cyclesShader->dereference();
    deviceState()->scene->delete_node(m_cyclesShader);
    m_cyclesShader = nullptr;
  }
}

void HDRI::rebuildEnvironmentShader()
{
  auto graph = std::make_unique<ccl::ShaderGraph>();

  // Build orthonormal basis from direction and up vectors (both already
  // normalized at commit); guard against 'up' parallel to 'direction'.
  auto forward = m_direction;
  auto up = m_up;
  auto right = math::cross(forward, up);
  if (math::length(right) < 1e-6f) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "hdri light 'up' is parallel to 'direction'; picking an "
        "arbitrary perpendicular up vector");
    up = std::abs(forward.z) < 0.99f ? math::float3{0.f, 0.f, 1.f}
                                     : math::float3{0.f, 1.f, 0.f};
    right = math::cross(forward, up);
  }
  right = math::normalize(right);
  up = math::normalize(math::cross(right, forward)); // Ensure orthogonality

  // Rotation from the standard basis to the custom orientation, as
  // axis-angle for the Cycles vector rotation node.
  math::mat3 rotationMat = {forward, right, up};
  auto rotation = math::rotation_quat(rotationMat);
  float angle = qangle(rotation);
  math::float3 axis = qaxis(rotation);

  auto tex_coords = graph->create_node<ccl::TextureCoordinateNode>();

  auto vectorRotate = graph->create_node<ccl::VectorRotateNode>();
  vectorRotate->set_rotate_type(ccl::NODE_VECTOR_ROTATE_TYPE_AXIS);
  vectorRotate->set_angle(angle);
  vectorRotate->set_axis(ccl::make_float3(axis.x, axis.y, axis.z));
  graph->connect(
      tex_coords->output("Generated"), vectorRotate->input("Vector"));

  // Create environment texture node
  auto *env_tex = graph->create_node<ccl::EnvironmentTextureNode>();
  env_tex->set_projection(ccl::NODE_ENVIRONMENT_EQUIRECTANGULAR);
  env_tex->set_colorspace(ccl::u_colorspace_data);
  env_tex->set_tex_mapping_type(ccl::TextureMapping::VECTOR);
  env_tex->set_tex_mapping_x_mapping(ccl::TextureMapping::X);
  env_tex->set_tex_mapping_y_mapping(ccl::TextureMapping::Y);
  env_tex->set_tex_mapping_z_mapping(ccl::TextureMapping::Z);
  env_tex->set_tex_mapping_scale(ccl::make_float3(1.0f, 1.0f, 1.0f));

  graph->connect(vectorRotate->output("Vector"), env_tex->input("Vector"));

  // Use SamplerImageLoader to get the image handle (identity-based
  // ImageLoader::equals() dedups repeated adds of the same source array).
  auto loader = std::make_unique<SamplerImageLoader>(m_radiance.ptr);
  ccl::ImageParams params;
  params.alpha_type = IMAGE_ALPHA_AUTO;
  params.interpolation = INTERPOLATION_LINEAR;

  env_tex->handle = deviceState()->scene->image_manager->add_image(
      std::move(loader), params, false);

  // Create output node
  auto *background = graph->create_node<ccl::BackgroundNode>();

  if (m_visible) {
    background->set_strength(m_scale);
    graph->connect(env_tex->output("Color"), background->input("Color"));
  } else {
    // KHR_AREA_LIGHTS 'visible' = false: camera rays see the renderer's
    // 'background' color (kept in sync via setCameraBackgroundColor())
    // while all other rays still see the scaled environment, so the HDRI
    // keeps illuminating the scene.
    auto *scaledEnv = graph->create_node<ccl::MixNode>();
    scaledEnv->set_mix_type(ccl::NODE_MIX_MUL);
    scaledEnv->set_fac(1.f);
    scaledEnv->set_color2(ccl::make_float3(m_scale, m_scale, m_scale));
    graph->connect(env_tex->output("Color"), scaledEnv->input("Color1"));

    auto *lightPath = graph->create_node<ccl::LightPathNode>();
    auto *mix = graph->create_node<ccl::MixNode>();
    mix->set_mix_type(ccl::NODE_MIX_BLEND);
    mix->set_color2(ccl::make_float3(
        m_cameraBgColor.x, m_cameraBgColor.y, m_cameraBgColor.z));
    graph->connect(lightPath->output("Is Camera Ray"), mix->input("Fac"));
    graph->connect(scaledEnv->output("Color"), mix->input("Color1"));

    background->set_strength(1.f);
    graph->connect(mix->output("Color"), background->input("Color"));
  }

  graph->connect(
      background->output("Background"), graph->output()->input("Surface"));

  // Assign the new graph, keeping the shader node itself stable (it is
  // referenced by scene->background between world rebuilds).
  if (!m_cyclesShader) {
    m_cyclesShader = deviceState()->scene->create_node<ccl::Shader>();
    m_cyclesShader->reference();
  }
  m_cyclesShader->set_graph(std::move(graph));
  m_cyclesShader->tag_update(deviceState()->scene);
}

void HDRI::setCameraBackgroundColor(const math::float3 &color)
{
  if (color == m_cameraBgColor)
    return;
  m_cameraBgColor = color; // remembered for future graph rebuilds
  // Only an invisible HDRI's shader bakes this color into its camera-ray
  // branch; a visible one shows the environment itself.
  if (m_visible || !m_cyclesShader || !m_radiance)
    return;
  rebuildEnvironmentShader();
  deviceState()->scene->background->tag_update(deviceState()->scene);
}

math::mat4 HDRI::xfm() const
{
  return math::mat4(1.0f);
}

} // namespace anari_cycles
