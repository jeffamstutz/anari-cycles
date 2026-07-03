// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Renderer.h"
// cycles
#include "scene/background.h"
#include "scene/integrator.h"
#include "scene/pass.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
// std
#include <algorithm>

namespace anari_cycles {

Renderer::Renderer(CyclesGlobalState *s) : Object(ANARI_RENDERER, s)
{
  commitParameters();
}

Renderer::~Renderer() = default;

void Renderer::commitParameters()
{
  auto backgroundColor =
      getParam<math::float4>("background", {0.f, 0.f, 0.f, 1.f});
  m_needsUpdateStatus.background |= (m_backgroundColor != backgroundColor);
  m_backgroundColor = backgroundColor;

  auto ambientColor = getParam<math::float3>("ambientColor", {1.f, 1.f, 1.f});
  m_needsUpdateStatus.ambientLight |= (m_ambientColor != ambientColor);
  m_ambientColor = ambientColor;
  auto ambientRadiance = getParam<float>("ambientRadiance", 1.f);
  m_needsUpdateStatus.ambientLight |= (m_ambientRadiance != ambientRadiance);
  m_ambientRadiance = ambientRadiance;

  m_runAsync = getParam<bool>("runAsync", true);

  m_pixelSamples = std::max(1, getParam<int>("pixelSamples", 1));

  auto denoise = getParam<bool>("denoise", false);
  m_needsUpdateStatus.denoise |= (m_denoise != denoise);
  m_denoise = denoise;
}

void Renderer::rebuildDefaultBackgroundShader()
{
  // Setup the background shader. Camera rays see the 'background' color,
  // while all other rays see 'ambientColor * ambientRadiance' — a uniform
  // dome implementing KHR_RENDERER_AMBIENT_LIGHT.
  auto graph = std::make_unique<ccl::ShaderGraph>();

  auto *lightPath = graph->create_node<ccl::LightPathNode>();

  auto *mix = graph->create_node<ccl::MixNode>();
  mix->set_mix_type(ccl::NODE_MIX_BLEND);
  mix->set_color1(m_ambientRadiance
      * ccl::make_float3(m_ambientColor.x, m_ambientColor.y, m_ambientColor.z));
  mix->set_color2(ccl::make_float3(
      m_backgroundColor.x, m_backgroundColor.y, m_backgroundColor.z));
  graph->connect(lightPath->output("Is Camera Ray"), mix->input("Fac"));

  auto *bg = graph->create_node<ccl::BackgroundNode>();
  graph->connect(mix->output("Color"), bg->input("Color"));

  graph->connect(bg->output("Background"), graph->output()->input("Surface"));

  deviceState()->scene->default_background->name = "anari_default_background";
  deviceState()->scene->default_background->set_graph(std::move(graph));
  deviceState()->scene->default_background->tag_update(deviceState()->scene);
}

void Renderer::makeRendererCurrent()
{
  if (m_needsUpdateStatus.background || m_needsUpdateStatus.ambientLight) {
    m_needsUpdateStatus.background = false;
    m_needsUpdateStatus.ambientLight = false;
    rebuildDefaultBackgroundShader();
  }
#if defined(WITH_OPTIX) || defined(WITH_OPENIMAGEDENOISE)
  if (m_needsUpdateStatus.denoise) {
    m_needsUpdateStatus.denoise = false;
    reportMessage(ANARI_SEVERITY_DEBUG,
        "renderer -- set_use_denoise(%s)",
        m_denoise ? "true" : "false");
    deviceState()->scene->integrator->set_use_denoise(m_denoise);
    // Cycles' finalize_passes() can only downgrade DENOISED→NOISY (when
    // denoise is off), never upgrade NOISY→DENOISED. Once a named pass
    // becomes NOISY it stays NOISY, causing the output driver to always
    // read the noisy buffer. Fix by restoring DENOISED mode on the named
    // combined pass before the scene update runs.
    if (m_denoise) {
      for (ccl::Pass *pass : deviceState()->scene->passes) {
        if (pass->get_type() == ccl::PASS_COMBINED && !pass->get_name().empty()
            && pass->get_mode() != ccl::PassMode::DENOISED) {
          pass->set_mode(ccl::PassMode::DENOISED);
        }
      }
    }
  }
#else
  (void)m_denoise;
  if (m_needsUpdateStatus.denoise) {
    m_needsUpdateStatus.denoise = false;
    reportMessage(ANARI_SEVERITY_WARNING,
        "renderer -- denoise requested but no denoiser compiled in");
  }
#endif
}

bool Renderer::runAsync() const
{
  return m_runAsync;
}

int Renderer::pixelSamples() const
{
  return m_pixelSamples;
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Renderer *);
