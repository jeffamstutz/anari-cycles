// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Renderer.h"
// cycles
#include "scene/background.h"
#include "scene/shader_nodes.h"

namespace cycles {

Renderer::Renderer(CyclesGlobalState *s) : Object(ANARI_RENDERER, s)
{
  commitParameters();
}

Renderer::~Renderer() = default;

void Renderer::commitParameters()
{
  m_backgroundColor =
      getParam<anari_vec::float4>("background", {0.f, 0.f, 0.f, 1.f});
  m_ambientColor = getParam<anari_vec::float3>("ambientColor", {1.f, 1.f, 1.f});
  m_ambientIntensity = 0.1f * getParam<float>("ambientRadiance", 1.f);
}

void Renderer::makeRendererCurrent() const
{
  auto &state = *deviceState();
  auto bgc = m_backgroundColor;

  for (auto &v : bgc)
    v = std::max(1e-6f, v);

  state.ambient->set_color(ccl::make_float3(
      m_ambientColor[0], m_ambientColor[1], m_ambientColor[2]));
  state.ambient->set_strength(m_ambientIntensity);

  state.background->set_color(ccl::make_float3(bgc[0], bgc[1], bgc[2]));
  state.background->set_strength(1.f);

  state.scene->default_background->tag_update(state.scene);
  state.scene->background->tag_update(state.scene);
}

} // namespace cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(cycles::Renderer *);
