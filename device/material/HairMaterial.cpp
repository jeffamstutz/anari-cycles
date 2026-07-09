// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "HairMaterial.h"
// std
#include <algorithm>
#include <cmath>

namespace anari_cycles {

HairMaterial::HairMaterial(CyclesGlobalState *s)
    : Material(s), m_colorSampler(this)
{}

void HairMaterial::commitParameters()
{
  m_colorMode = getParamString("colorMode", "color");
  m_model = getParamString("model", "huang");

  m_colorAttr = getParamString("color", "");
  m_color = getParam<float3>(
      "color", make_float3(0.017513f, 0.005763f, 0.002059f));
  m_colorSampler = getParamObject<Sampler>("color");

  m_melanin = getParam<float>("melanin", 0.8f);
  m_melaninRedness = getParam<float>("melaninRedness", 1.f);
  m_tint = getParam<float3>("tint", make_float3(1.f, 1.f, 1.f));
  m_absorptionCoefficient = getParam<float3>(
      "absorptionCoefficient", make_float3(0.245531f, 0.52f, 1.365f));

  m_roughness = getParam<float>("roughness", 0.3f);
  m_radialRoughness = getParam<float>("radialRoughness", 0.3f);
  m_coat = getParam<float>("coat", 0.f);
  m_ior = getParam<float>("ior", 1.55f);
  m_offset = getParam<float>("offset", 2.f * M_PI_F / 180.f);
  m_randomColor = getParam<float>("randomColor", 0.f);
  m_randomRoughness = getParam<float>("randomRoughness", 0.f);
  m_aspectRatio = getParam<float>("aspectRatio", 0.85f);

  if (m_colorMode != "color" && m_colorMode != "melanin"
      && m_colorMode != "absorption") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "hair material 'colorMode' must be 'color', 'melanin' or "
        "'absorption' (got '%s'); using 'color'",
        m_colorMode.c_str());
    m_colorMode = "color";
  }
  if (m_model != "huang" && m_model != "chiang") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "hair material 'model' must be 'huang' or 'chiang' (got '%s'); "
        "using 'huang'",
        m_model.c_str());
    m_model = "huang";
  }
}

void HairMaterial::finalize()
{
  makeGraph();

  m_bsdf->set_model(m_model == "chiang" ? ccl::NODE_PRINCIPLED_HAIR_CHIANG
                                        : ccl::NODE_PRINCIPLED_HAIR_HUANG);
  if (m_colorMode == "melanin") {
    m_bsdf->set_parametrization(ccl::NODE_PRINCIPLED_HAIR_PIGMENT_CONCENTRATION);
  } else if (m_colorMode == "absorption") {
    m_bsdf->set_parametrization(ccl::NODE_PRINCIPLED_HAIR_DIRECT_ABSORPTION);
  } else {
    m_bsdf->set_parametrization(ccl::NODE_PRINCIPLED_HAIR_REFLECTANCE);
  }

  connectAttributes(m_bsdf, m_colorAttr, "Color", m_color, m_colorSampler.get());
  m_bsdf->input("Melanin")->set(m_melanin);
  m_bsdf->input("Melanin Redness")->set(m_melaninRedness);
  m_bsdf->input("Tint")->set(m_tint);
  m_bsdf->input("Absorption Coefficient")->set(m_absorptionCoefficient);
  m_bsdf->input("Roughness")->set(m_roughness);
  m_bsdf->input("Radial Roughness")->set(m_radialRoughness);
  m_bsdf->input("Coat")->set(m_coat);
  m_bsdf->input("IOR")->set(m_ior);
  m_bsdf->input("Offset")->set(m_offset);
  m_bsdf->input("Random Color")->set(m_randomColor);
  m_bsdf->input("Random Roughness")->set(m_randomRoughness);
  m_bsdf->input("Aspect Ratio")->set(m_aspectRatio);

  Material::finalize();
}

void HairMaterial::makeGraph()
{
  Material::makeGraph();
  m_bsdf = m_graph->create_node<ccl::PrincipledHairBsdfNode>();
  m_graph->connect(m_bsdf->output("BSDF"), m_graph->output()->input("Surface"));
}

} // namespace anari_cycles
