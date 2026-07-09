// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "MatteMaterial.h"
// std
#include <algorithm>
#include <cmath>

namespace anari_cycles {

MatteMaterial::MatteMaterial(CyclesGlobalState *s)
    : Material(s), m_colorSampler(this), m_opacitySampler(this)
{}

void MatteMaterial::commitParameters()
{
  m_colorAttr = getParamString("color", "");
  m_color = getParam<float3>("color", make_float3(0.8f, 0.8f, 0.8f));
  m_colorSampler = getParamObject<Sampler>("color");
  m_opacityAttr = getParamString("opacity", "");
  m_opacity = getParam<float>("opacity", 1.f);
  m_opacitySampler = getParamObject<Sampler>("opacity");
  m_mode = helium::alphaModeFromString(getParamString("alphaMode", "opaque"));
  m_alphaCutoff = getParam<float>("alphaCutoff", 0.5f);
}

void MatteMaterial::finalize()
{
  makeGraph();

  connectAttributes(
      m_bsdf, m_colorAttr, "Base Color", m_color, m_colorSampler.get());

  connectAlpha(m_bsdf,
      m_opacityAttr,
      m_opacity,
      m_opacitySampler.get(),
      m_colorAttr,
      m_colorSampler.get(),
      m_mode,
      m_alphaCutoff);

  Material::finalize();
}

void MatteMaterial::makeGraph()
{
  Material::makeGraph();
  m_bsdf = m_graph->create_node<ccl::PrincipledBsdfNode>();
  m_graph->connect(m_bsdf->output("BSDF"), m_graph->output()->input("Surface"));
  m_bsdf->input("Roughness")->set(1.f);
  m_bsdf->input("Metallic")->set(0.f);
  m_bsdf->input("Coat Weight")->set(0.f);
  m_bsdf->input("Transmission Weight")->set(0.f);
}

} // namespace anari_cycles
