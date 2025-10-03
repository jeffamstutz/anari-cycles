// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Renderer.h"
// cycles
#include "scene/background.h"
#include "scene/light.h"
#include "scene/shader_nodes.h"
#include "scene/shader_graph.h"

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
  m_needsUpdateStatus.background |=
      (m_backgroundColor != backgroundColor);
  m_backgroundColor = backgroundColor;

  auto ambientColor = getParam<math::float3>("ambientColor", {1.f, 1.f, 1.f});
  m_needsUpdateStatus.ambientLight |=
      (m_ambientColor != ambientColor);
  m_ambientColor = ambientColor;
  auto ambientIntensity = 0.1f * getParam<float>("ambientRadiance", 1.f);
  m_needsUpdateStatus.ambientLight |=
      (m_ambientIntensity != ambientIntensity);
  m_ambientIntensity = ambientIntensity;

  m_runAsync = getParam<bool>("runAsync", true);
}

void Renderer::rebuildDefaultBackgroundShader()
{
  // setup background shader. Only keep background color for the background itself
  // and kill illumination from other rays.
  // Ambient lighting is handled through the default light shader.
  auto graph = std::make_unique<ccl::ShaderGraph>();

  auto *lightPath = graph->create_node<ccl::LightPathNode>();
  auto *bg = graph->create_node<ccl::BackgroundNode>();

  auto  mathR = graph->create_node<ccl::MathNode>();
  mathR->set_math_type(ccl::NODE_MATH_MULTIPLY);
  mathR->set_value1(m_backgroundColor.x);
  graph->connect(lightPath->output("Is Camera Ray"), mathR->input("Value2"));

  auto  mathG = graph->create_node<ccl::MathNode>();
  mathG->set_math_type(ccl::NODE_MATH_MULTIPLY);
  mathG->set_value1(m_backgroundColor.y);
  graph->connect(lightPath->output("Is Camera Ray"), mathG->input("Value2"));

  auto  mathB = graph->create_node<ccl::MathNode>();
  mathB->set_math_type(ccl::NODE_MATH_MULTIPLY);
  mathB->set_value1(m_backgroundColor.z);
  graph->connect(lightPath->output("Is Camera Ray"), mathB->input("Value2"));

  auto combineColor = graph->create_node<ccl::CombineRGBNode>();
  graph->connect(mathR->output("Value"), combineColor->input("R"));
  graph->connect(mathG->output("Value"), combineColor->input("G"));
  graph->connect(mathB->output("Value"), combineColor->input("B"));
  graph->connect(combineColor->output("Image"), bg->input("Color"));

  graph->connect(bg->output("Background"), graph->output()->input("Surface"));

  deviceState()->scene->default_background->name = "anari_default_background";
  deviceState()->scene->default_background->set_graph(std::move(graph));
  deviceState()->scene->default_background->tag_update(deviceState()->scene);
}

void Renderer::rebuildDefaultLightShader()
{
  auto graph = std::make_unique<ccl::ShaderGraph>();

  auto emission = graph->create_node<ccl::EmissionNode>();
  emission->set_color(ccl::make_float3(m_ambientColor.x, m_ambientColor.y, m_ambientColor.z));
  emission->set_strength(m_ambientIntensity * 40.0f);

  graph->connect(emission->output("Emission"), graph->output()->input("Surface"));

  deviceState()->scene->default_light->name = "default_anari_light";
  deviceState()->scene->default_light->set_graph(std::move(graph));

  deviceState()->scene->default_light->tag_update(deviceState()->scene);
}

void Renderer::makeRendererCurrent()
{
  if (m_needsUpdateStatus.background) {
    m_needsUpdateStatus.background = false;
    rebuildDefaultBackgroundShader();
  }
  if (m_needsUpdateStatus.ambientLight) {
    m_needsUpdateStatus.ambientLight = false;
    rebuildDefaultLightShader();
  }
}

bool Renderer::runAsync() const
{
  return m_runAsync;
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Renderer *);
