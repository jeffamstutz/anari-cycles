// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Sky.h"
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

Sky::Sky(CyclesGlobalState *s)
    : Light(s, s->scene->create_node<ccl::BackgroundLight>())
{}

Sky::~Sky() = default;

void Sky::commitParameters()
{
  Light::commitParameters();

  m_sunDirection = getNormalizedDirection("sunDirection", {0.f, 0.f, 1.f});
  m_sunDisc = getParam<bool>("sunDisc", true);
  // Full angular diameter of the sun disc, matching the SkyTextureNode
  // 'sun_size' socket (default is the real sun, ~0.545 degrees).
  m_sunSize =
      std::clamp(getParam<float>("sunSize", 0.009512f), 0.f, float(M_PI));
  m_sunIntensity = std::max(getParam<float>("sunIntensity", 1.f), 0.f);
  m_scale = std::max(getParam<float>("scale", 1.f), 0.f);
  // Atmosphere parameters, clamped to the ranges the Nishita precompute is
  // designed for (the Blender UI ranges).
  m_altitude = std::clamp(getParam<float>("altitude", 0.f), 0.f, 60000.f);
  m_airDensity = std::clamp(getParam<float>("airDensity", 1.f), 0.f, 10.f);
  m_dustDensity = std::clamp(getParam<float>("dustDensity", 1.f), 0.f, 10.f);
  m_ozoneDensity = std::clamp(getParam<float>("ozoneDensity", 1.f), 0.f, 10.f);
}

void Sky::finalize()
{
  Light::finalize();
  m_cyclesLight->tag_update(deviceState()->scene);
  rebuildSkyShader();
}

void Sky::rebuildSkyShader()
{
  auto graph = std::make_unique<ccl::ShaderGraph>();

  // The sky dome lives in world space with +Z up: the node's unconnected
  // 'Vector' input defaults to the generated coordinates, which in a
  // background shader are the world-space ray direction.
  auto *sky = graph->create_node<ccl::SkyTextureNode>();
  sky->set_sky_type(ccl::NODE_SKY_MULTIPLE_SCATTERING);
  sky->set_sun_disc(m_sunDisc);
  sky->set_sun_size(m_sunSize);
  sky->set_sun_intensity(m_sunIntensity);

  // Map 'sunDirection' onto the node's elevation/rotation sockets. With the
  // node's conventions (simplify_settings() flips the rotation sign before
  // the kernel's spherical_to_direction()), the sun ends up at
  // (cos(e)*sin(r), cos(e)*cos(r), sin(e)) in world space.
  const float elevation = std::atan2(
      m_sunDirection.z, std::hypot(m_sunDirection.x, m_sunDirection.y));
  const float rotation = std::atan2(m_sunDirection.x, m_sunDirection.y);
  sky->set_sun_elevation(elevation);
  sky->set_sun_rotation(rotation);

  sky->set_altitude(m_altitude);
  sky->set_air_density(m_airDensity);
  sky->set_aerosol_density(m_dustDensity);
  sky->set_ozone_density(m_ozoneDensity);

  // Apply the overall 'scale' and 'color' tint (sun disc included).
  auto *tint = graph->create_node<ccl::MixNode>();
  tint->set_mix_type(ccl::NODE_MIX_MUL);
  tint->set_fac(1.f);
  tint->set_color2(scaledColor(m_scale));
  graph->connect(sky->output("Color"), tint->input("Color1"));

  auto *background = graph->create_node<ccl::BackgroundNode>();
  background->set_strength(1.f);

  if (m_visible) {
    graph->connect(tint->output("Color"), background->input("Color"));
  } else {
    // 'visible' = false: camera rays see the renderer's 'background' color
    // (kept in sync via setCameraBackgroundColor()) while all other rays
    // still see the sky, so it keeps illuminating the scene (same scheme as
    // an invisible HDRI).
    auto *lightPath = graph->create_node<ccl::LightPathNode>();
    auto *mix = graph->create_node<ccl::MixNode>();
    mix->set_mix_type(ccl::NODE_MIX_BLEND);
    mix->set_color2(ccl::make_float3(
        m_cameraBgColor.x, m_cameraBgColor.y, m_cameraBgColor.z));
    graph->connect(lightPath->output("Is Camera Ray"), mix->input("Fac"));
    graph->connect(tint->output("Color"), mix->input("Color1"));
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

void Sky::setCameraBackgroundColor(const math::float3 &color)
{
  if (color == m_cameraBgColor)
    return;
  m_cameraBgColor = color; // remembered for future graph rebuilds
  // Only an invisible sky's shader bakes this color into its camera-ray
  // branch; a visible one shows the sky dome itself.
  if (m_visible || !m_cyclesShader)
    return;
  rebuildSkyShader();
  deviceState()->scene->background->tag_update(deviceState()->scene);
}

math::mat4 Sky::xfm() const
{
  // The dome is evaluated in world space; instance transforms do not
  // reorient it (documented in the extension JSON).
  return math::mat4(1.0f);
}

} // namespace anari_cycles
