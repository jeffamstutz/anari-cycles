// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Light.h"
#include <anari/anari_cpp/ext/linalg.h>
#include <cmath>
#include <cstdio>
#include <string>
#include "Sampler.h"

// cycles
#include "SamplerImageLoader.h"
#include "kernel/svm/types.h"
#include "scene/background.h"
#include "scene/camera.h"
#include "scene/shader.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "util/colorspace.h"
#include "util/math_base.h"
#include "util/transform.h"
#include "util/types_float3.h"

namespace anari_cycles {

// Helper functions ///////////////////////////////////////////////////////////

// Stand-in radius for degenerate (zero-radius) ring lights.
static constexpr float g_minRingRadius = 1e-3f;

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

// Transform for a light positioned at 'position' emitting along 'direction'
// (the Cycles light-local -Z axis).
inline math::mat4 positionDirectionXfm(
    const math::float3 &position, const math::float3 &direction)
{
  auto rot = math::inverse(rotationFromZNegativeToTarget(direction));
  rot[3] = {position.x, position.y, position.z, 1.f};
  return rot;
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
  float m_angularDiameter{0.f};
  float m_strengthValue{1.f};
  bool m_usesRadiance{false};
};

struct HDRI : public Light
{
  HDRI(CyclesGlobalState *s);
  ~HDRI() override;

  void commitParameters() override;
  void finalize() override;
  math::mat4 xfm() const override;

  void setCameraBackgroundColor(const math::float3 &color) override;

 private:
  // (Re)build the environment shader graph from the committed parameters
  // and m_cameraBgColor. Keeps the ccl::Shader node itself stable so
  // scene->background's shader pointer stays valid across rebuilds.
  void rebuildEnvironmentShader();

  helium::IntrusivePtr<Array2D> m_radiance{};
  math::float3 m_up{0.f, 0.f, 1.f};
  math::float3 m_direction{1.f, 0.f, 0.f};

  float m_scale{1.f};

  // When not 'visible', camera rays see this solid color instead of the
  // environment; synced to the active renderer's 'background' each frame.
  // Baked into the shader graph as a constant (cached ShaderNode pointers
  // are unsafe: Cycles constant-folds and frees nodes on compile), so a
  // color change rebuilds the graph.
  math::float3 m_cameraBgColor{0.f, 0.f, 0.f};
};

struct Point : public Light
{
  Point(CyclesGlobalState *s);

  void commitParameters() override;
  void finalize() override;
  math::mat4 xfm() const override;

 private:
  enum class Quantity { RADIANCE, INTENSITY, POWER };
  math::float3 m_position{0.f, 0.f, 0.f};
  Quantity m_quantity{Quantity::INTENSITY};
  float m_value{1.f};
  float m_radius{0.f};
};

struct Spot : public Light
{
  Spot(CyclesGlobalState *s);

  void commitParameters() override;
  void finalize() override;
  math::mat4 xfm() const override;

 private:
  math::float3 m_position{0.f, 0.f, 0.f};
  math::float3 m_direction{0.f, 0.f, -1.f};
  float m_value{1.f};
  bool m_usesPower{false};
  float m_openingAngle{M_PI};
  float m_falloffAngle{0.1f};
  float m_radius{0.f};
};

struct Ring : public Light
{
  Ring(CyclesGlobalState *s);

  void commitParameters() override;
  void finalize() override;
  math::mat4 xfm() const override;

 private:
  math::float3 m_position{0.f, 0.f, 0.f};
  math::float3 m_direction{0.f, 0.f, -1.f};
  float m_openingAngle{M_PI};
  float m_radius{0.f};
  float m_effectiveRadius{0.f};
  float m_innerRadius{0.f};
  float m_radiance{1.f};
  bool m_falloffAngleSet{false};
};

struct QuadLight : public Light
{
  QuadLight(CyclesGlobalState *s);
  ~QuadLight() override;

  bool isValid() const override;
  void commitParameters() override;
  void finalize() override;
  math::mat4 xfm() const override;
  ccl::Light *secondaryCyclesLight() const override;
  math::mat4 secondaryXfm() const override;

 private:
  math::mat4 quadXfm(bool backSide) const;

  // Second emitter for side='both' (a Cycles area light only emits from one
  // hemisphere); shares the unit-emission shader, only instanced when used.
  ccl::Light *m_cyclesLightBack{nullptr};

  math::float3 m_position{0.f, 0.f, 0.f};
  math::float3 m_edge1{1.f, 0.f, 0.f};
  math::float3 m_edge2{0.f, 1.f, 0.f};
  float m_area{1.f};
  float m_radiance{1.f};
  std::string m_side{"front"};
};

struct UnknownLight : public Light
{
  UnknownLight(std::string_view subtype, CyclesGlobalState *s);

  bool isValid() const override;
  void warnIfUnknownObject() const override;
  math::mat4 xfm() const override;

 private:
  std::string m_subtype;
};

// Light definitions //////////////////////////////////////////////////////////

Light::Light(CyclesGlobalState *s, ccl::Light *light)
    : Object(ANARI_LIGHT, s), m_cyclesLight(light)
{}

Light::~Light()
{
  // Object release can happen while the render thread reads the scene, and
  // scene->objects may still reference the light node -- defer its deletion.
  CyclesGlobalState::SceneLock sceneLock(*deviceState());
  deviceState()->retireGeometry(m_cyclesLight);
  if (m_cyclesShader) {
    m_cyclesShader->dereference();
    // delete_node(Shader*) only clears the reference count; Cycles never
    // frees shaders before the scene itself is destroyed.
    deviceState()->scene->delete_node(m_cyclesShader);
  }
}

void Light::attachUnitEmissionShader()
{
  auto graph = std::make_unique<ccl::ShaderGraph>();

  auto *emission = graph->create_node<ccl::EmissionNode>();
  emission->set_color(ccl::one_float3());
  emission->set_strength(1.f);
  graph->connect(
      emission->output("Emission"), graph->output()->input("Surface"));

  m_cyclesShader = deviceState()->scene->create_node<ccl::Shader>();
  m_cyclesShader->name = "anari_light_emission";
  m_cyclesShader->set_graph(std::move(graph));
  m_cyclesShader->reference();
  m_cyclesShader->tag_update(deviceState()->scene);

  ccl::array<ccl::Node *> usedShaders;
  usedShaders.push_back_slow(m_cyclesShader);
  m_cyclesLight->set_used_shaders(usedShaders);

  // MIS lets camera rays display the light geometry (KHR_AREA_LIGHTS
  // 'visible', default true; the per-instance camera-visibility switch is
  // in Group.cpp). Cycles' own default is false.
  m_cyclesLight->set_use_mis(true);
}

ccl::float3 Light::scaledColor(float scale) const
{
  return scale * ccl::make_float3(m_color[0], m_color[1], m_color[2]);
}

float Light::photometricRadiance(float area)
{
  // ANARI area-light photometric precedence: 'radiance' wins over
  // 'intensity' (W/sr, divided by the emitting area) over 'power' (W,
  // divided by pi times the area for a Lambertian emitter).
  //
  // LIMITATION: 'area' is computed from the light's LOCAL parameters at
  // commit time, but instance transforms (Group.cpp) are applied later and
  // may scale the emitter. Since the Cycles lights are configured with
  // normalize off (emitted radiance independent of world-space area), only
  // 'radiance' is exact under instance scaling; 'intensity' and 'power' are
  // only exact for unscaled instances. Fixing this would require per-instance
  // light nodes; documented in the device's extension JSON instead.
  float radiance = 1.f;
  if (hasParam("radiance", ANARI_FLOAT32)) {
    radiance = getParam<float>("radiance", 1.f);
  } else if (hasParam("intensity", ANARI_FLOAT32) && area > 0.f) {
    radiance = getParam<float>("intensity", 1.f) / area;
  } else if (hasParam("power", ANARI_FLOAT32) && area > 0.f) {
    radiance = getParam<float>("power", 1.f) / (float(M_PI) * area);
  }
  return std::clamp(radiance, 0.f, std::numeric_limits<float>::max());
}

Light *Light::createInstance(std::string_view type, CyclesGlobalState *s)
{
  if (type == "directional")
    return new Directional(s);
  else if (type == "hdri")
    return new HDRI(s);
  else if (type == "point")
    return new Point(s);
  else if (type == "spot")
    return new Spot(s);
  else if (type == "quad")
    return new QuadLight(s);
  else if (type == "ring")
    return new Ring(s);
  else
    return new UnknownLight(type, s);
}

math::float3 Light::getNormalizedDirection(
    const char *name, const math::float3 &fallback)
{
  const auto dir = getParam<math::float3>(name, fallback);
  const float len = math::length(dir);
  if (!std::isfinite(len) || len <= 0.f) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "light '%s' parameter is zero-length or non-finite; "
        "using (%g, %g, %g)",
        name,
        double(fallback.x),
        double(fallback.y),
        double(fallback.z));
    return fallback;
  }
  return dir / len;
}

void Light::commitParameters()
{
  m_color = getParam<anari_vec::float3>("color", {1.f, 1.f, 1.f});
  // KHR_AREA_LIGHTS: light geometry is visible to camera rays by default.
  m_visible = getParam<bool>("visible", true);
}

bool Light::visibleToCamera() const
{
  return m_visible;
}

ccl::Light *Light::secondaryCyclesLight() const
{
  return nullptr;
}

math::mat4 Light::secondaryXfm() const
{
  return math::mat4(linalg::identity);
}

void Light::setCameraBackgroundColor(const math::float3 &)
{
  // only meaningful for HDRI lights
}

void Light::finalize()
{
  // Light state is baked into per-instance ccl::Objects at world-rebuild
  // time (transform, camera visibility, secondary emitters), and neither
  // helium object arrays nor Object::markFinalized() propagate light
  // commits to the world -- invalidate the baked scene objects here so any
  // light change triggers a rebuild on the next frame.
  // TODO: make light updates more efficient (rebuilds the whole world).
  deviceState()->objectUpdates.lastSceneChange = helium::newTimeStamp();
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

// HDRI definitions ///////////////////////////////////////////////////////////

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

// Point definitions //////////////////////////////////////////////////////////

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

// Spot definitions ///////////////////////////////////////////////////////////

Spot::Spot(CyclesGlobalState *s)
    : Light(s, s->scene->create_node<ccl::SpotLight>())
{
  attachUnitEmissionShader();
}

void Spot::commitParameters()
{
  Light::commitParameters();
  m_position = getParam<math::float3>("position", {0.f, 0.f, 0.f});
  m_direction = getNormalizedDirection("direction", {0.f, 0.f, -1.f});
  // Photometric precedence: 'intensity' (W/sr) over 'power' (W).
  m_usesPower =
      !hasParam("intensity", ANARI_FLOAT32) && hasParam("power", ANARI_FLOAT32);
  m_value = std::clamp(m_usesPower ? getParam<float>("power", 1.f)
                                   : getParam<float>("intensity", 1.f),
      0.f,
      std::numeric_limits<float>::max());
  // ANARI 'openingAngle' is the full apex angle of the cone (default pi),
  // matching the Cycles spot 'angle' socket.
  m_openingAngle =
      std::clamp(getParam<float>("openingAngle", float(M_PI)), 0.f, float(M_PI));
  m_falloffAngle = getParam<float>("falloffAngle", 0.1f);
  m_radius = getParam<float>("radius", 0.f);
}

void Spot::finalize()
{
  auto *light = static_cast<ccl::SpotLight *>(m_cyclesLight);
  light->set_radius(m_radius);
  light->set_angle(m_openingAngle);
  // Cycles 'smooth' is the fraction of the cone half-angle over which
  // intensity falls off toward the rim; ANARI 'falloffAngle' is that
  // region's angular size.
  const float halfAngle = 0.5f * m_openingAngle;
  light->set_smooth(
      halfAngle > 0.f ? std::clamp(m_falloffAngle / halfAngle, 0.f, 1.f) : 0.f);
  light->set_normalize(true);
  // Same conversions as point lights (the cone only masks emission; neither
  // ANARI nor Cycles renormalizes flux into the cone): 'intensity' (W/sr)
  // maps to normalized strength 4*pi*intensity, 'power' (W) is the
  // normalized strength itself.
  m_cyclesLight->set_strength(
      scaledColor(m_usesPower ? m_value : 4.f * float(M_PI) * m_value));
  m_cyclesLight->tag_update(deviceState()->scene);
  Light::finalize();
}

math::mat4 Spot::xfm() const
{
  return positionDirectionXfm(m_position, m_direction);
}

// Ring definitions ///////////////////////////////////////////////////////////

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
}

void Ring::finalize()
{
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
  Light::finalize();
}

math::mat4 Ring::xfm() const
{
  return positionDirectionXfm(m_position, m_direction);
}

// Quad definitions ///////////////////////////////////////////////////////////

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

// UnknownLight definitions ///////////////////////////////////////////////////

UnknownLight::UnknownLight(std::string_view subtype, CyclesGlobalState *s)
    : Light(s, nullptr), m_subtype(subtype)
{
  reportMessage(ANARI_SEVERITY_WARNING,
      "created unknown %s object of subtype '%s'",
      anari::toString(ANARI_LIGHT),
      m_subtype.c_str());
}

bool UnknownLight::isValid() const
{
  return false;
}

void UnknownLight::warnIfUnknownObject() const
{
  reportMessage(ANARI_SEVERITY_WARNING,
      "encountered unknown %s object of subtype '%s'",
      anari::toString(ANARI_LIGHT),
      m_subtype.c_str());
}

math::mat4 UnknownLight::xfm() const
{
  return math::mat4(1.f);
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Light *);
