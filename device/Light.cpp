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
  math::float3 m_up{0.f, 0.f, 1.f};
  math::float3 m_direction{1.f, 0.f, 0.f};

  float m_scale{1.f};
  bool m_visible{true};
};

struct Point : public Light
{
  Point(CyclesGlobalState *s);

  void commitParameters() override;
  void finalize() override;
  math::mat4 xfm() const override;

 private:
  math::float3 m_position{0.f, 0.f, 0.f};
  float m_intensity{1.f};
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
  float m_intensity{1.f};
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

  void commitParameters() override;
  void finalize() override;
  math::mat4 xfm() const override;

 private:
  math::float3 m_position{0.f, 0.f, 0.f};
  math::float3 m_edge1{1.f, 0.f, 0.f};
  math::float3 m_edge2{0.f, 1.f, 0.f};
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

Directional::Directional(CyclesGlobalState *s)
    : Light(s, s->scene->create_node<ccl::SunLight>())
{
  attachUnitEmissionShader();
}

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
  if (m_prevDirection != m_direction) {
    reportMessage(ANARI_SEVERITY_PERFORMANCE_WARNING,
        "make light updates more efficient!");
    deviceState()->objectUpdates.lastSceneChange = helium::newTimeStamp();
    m_prevDirection = m_direction;
  }

  m_cyclesLight->set_strength(scaledColor(m_irradiance));
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
  m_visible = getParam<bool>("visible", true);

  m_up = getParam<math::float3>("up", {0.f, 0.f, 1.f});
  m_direction = getParam<math::float3>("direction", {1.f, 0.f, 0.f});
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

  if (m_cyclesShader) {
    m_cyclesShader->dereference();
    deviceState()->scene->delete_node(m_cyclesShader);
    m_cyclesShader = nullptr;
  }

  // Create shader for HDRI if radiance is provided
  if (m_radiance) {
    auto graph = std::make_unique<ccl::ShaderGraph>();

    // Build orthonormal basis from direction and up vectors
    // We should ensure that up is not parallel to forward, let save that for
    // later.
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
    math::mat3 rotationMat = {forward, right, up};

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

    // Use SamplerImageLoader to get the image handle
    auto loader = std::make_unique<SamplerImageLoader>(m_radiance.ptr);
    ccl::ImageParams params;
    params.alpha_type = IMAGE_ALPHA_AUTO;
    params.interpolation = INTERPOLATION_LINEAR;

    env_tex->handle = deviceState()->scene->image_manager->add_image(
        std::move(loader), params, false);

    // Create output node
    auto *background = graph->create_node<ccl::BackgroundNode>();
    background->set_strength(m_scale);

    // Connect environment texture to background
    graph->connect(env_tex->output("Color"), background->input("Color"));

    graph->connect(
        background->output("Background"), graph->output()->input("Surface"));

    // Create shader and assign graph
    m_cyclesShader = deviceState()->scene->create_node<ccl::Shader>();
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
  m_intensity = std::clamp(getParam<float>("intensity", 1.f),
      0.f,
      std::numeric_limits<float>::max());
  m_radius = getParam<float>("radius", 0.f);
}

void Point::finalize()
{
  static_cast<ccl::PointLight *>(m_cyclesLight)->set_radius(m_radius);
  // ANARI 'intensity' is radiant intensity (W/sr); Cycles interprets strength
  // as total radiant flux (W) when 'normalize' is on (the default), so the
  // isotropic conversion is flux = 4*pi * intensity.
  m_cyclesLight->set_strength(scaledColor(4.f * float(M_PI) * m_intensity));
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
  m_direction =
      math::normalize(getParam<math::float3>("direction", {0.f, 0.f, -1.f}));
  m_intensity = std::clamp(getParam<float>("intensity", 1.f),
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
  // Same W/sr -> W conversion as point lights (the cone only masks emission;
  // Cycles does not renormalize flux into the cone).
  m_cyclesLight->set_strength(scaledColor(4.f * float(M_PI) * m_intensity));
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
  m_direction =
      math::normalize(getParam<math::float3>("direction", {0.f, 0.f, -1.f}));
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
}

void QuadLight::commitParameters()
{
  Light::commitParameters();
  m_position = getParam<math::float3>("position", {0.f, 0.f, 0.f});
  m_edge1 = getParam<math::float3>("edge1", {1.f, 0.f, 0.f});
  m_edge2 = getParam<math::float3>("edge2", {0.f, 1.f, 0.f});
  m_radiance =
      photometricRadiance(math::length(math::cross(m_edge1, m_edge2)));
  m_side = getParamString("side", "front");
}

void QuadLight::finalize()
{
  if (m_side == "both") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "quad light side='both' is not supported; using side='front'");
  } else if (m_side != "front" && m_side != "back") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "invalid quad light side '%s'; using side='front'",
        m_side.c_str());
    m_side = "front";
  }

  auto *light = static_cast<ccl::AreaLight *>(m_cyclesLight);
  light->set_sizeu(1.f);
  light->set_sizev(1.f);
  light->set_ellipse(false);
  light->set_spread(float(M_PI));
  // With normalize off, the emitted radiance is strength/pi independent of
  // the light's area (kernel eval_fac = invarea/pi with invarea = 1), so
  // ANARI 'radiance' maps to strength = pi * radiance. This also keeps the
  // radiance invariant under instance scaling.
  light->set_normalize(false);
  m_cyclesLight->set_strength(scaledColor(float(M_PI) * m_radiance));
  m_cyclesLight->tag_update(deviceState()->scene);
  Light::finalize();
}

math::mat4 QuadLight::xfm() const
{
  const auto center = m_position + 0.5f * (m_edge1 + m_edge2);

  auto normal = math::cross(m_edge1, m_edge2);
  if (math::length(normal) > 0.f)
    normal = math::normalize(normal);
  else
    normal = {0.f, 0.f, 1.f};

  if (m_side == "back")
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
