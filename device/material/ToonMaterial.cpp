// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "ToonMaterial.h"
// std
#include <algorithm>
#include <cmath>

namespace anari_cycles {

ToonMaterial::ToonMaterial(CyclesGlobalState *s)
    : Material(s), m_colorSampler(this), m_sizeSampler(this), m_smoothSampler(this)
{}

void ToonMaterial::commitParameters()
{
  m_colorAttr = getParamString("color", "");
  m_color = getParam<float3>("color", make_float3(0.8f, 0.8f, 0.8f));
  m_colorSampler = getParamObject<Sampler>("color");
  m_component = getParamString("component", "diffuse");
  m_sizeAttr = getParamString("size", "");
  m_size = getParam<float>("size", 0.5f);
  m_sizeSampler = getParamObject<Sampler>("size");
  m_smoothAttr = getParamString("smooth", "");
  m_smooth = getParam<float>("smooth", 0.f);
  m_smoothSampler = getParamObject<Sampler>("smooth");

  if (m_component != "diffuse" && m_component != "glossy") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "toon material 'component' must be 'diffuse' or 'glossy' (got '%s'); "
        "using 'diffuse'",
        m_component.c_str());
    m_component = "diffuse";
  }
}

void ToonMaterial::finalize()
{
  makeGraph();

  m_bsdf->set_component(m_component == "glossy" ? ccl::CLOSURE_BSDF_GLOSSY_TOON_ID
                                                : ccl::CLOSURE_BSDF_DIFFUSE_TOON_ID);
  connectAttributes(m_bsdf, m_colorAttr, "Color", m_color, m_colorSampler.get());
  connectAttributes(m_bsdf, m_sizeAttr, "Size", m_size, m_sizeSampler.get());
  connectAttributes(
      m_bsdf, m_smoothAttr, "Smooth", m_smooth, m_smoothSampler.get());

  Material::finalize();
}

void ToonMaterial::makeGraph()
{
  Material::makeGraph();
  m_bsdf = m_graph->create_node<ccl::ToonBsdfNode>();
  m_graph->connect(m_bsdf->output("BSDF"), m_graph->output()->input("Surface"));
}

} // namespace anari_cycles
