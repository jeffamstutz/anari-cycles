// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Material.h"

namespace cycles {

// MatteMaterial definitions //////////////////////////////////////////////////

struct MatteMaterial : public Material
{
  MatteMaterial(CyclesGlobalState *s);
  ~MatteMaterial() override = default;

  void commitParameters() override;
  void finalize() override;

 private:
  ccl::DiffuseBsdfNode *m_bsdf{nullptr};
  std::string m_colorMode;
  float3 m_color;
};

MatteMaterial::MatteMaterial(CyclesGlobalState *s) : Material(s)
{
  auto *bsdf = m_graph->create_node<ccl::DiffuseBsdfNode>();
  m_graph->connect(bsdf->output("BSDF"), m_graph->output()->input("Surface"));
  m_bsdf = bsdf;
}

void MatteMaterial::commitParameters()
{
  m_colorMode = getParamString("color", "");
  m_color = getParam<float3>("color", make_float3(1.f, 1.f, 1.f));
}

void MatteMaterial::finalize()
{
  connectAttributes(m_bsdf, m_colorMode, "Color", m_color, false);
  m_shader->tag_update(deviceState()->scene);
}

// PhysicallyBasedMaterial ////////////////////////////////////////////////////

struct PhysicallyBasedMaterial : public Material
{
  PhysicallyBasedMaterial(CyclesGlobalState *s);
  ~PhysicallyBasedMaterial() override = default;

  void commitParameters() override;
  void finalize() override;

 private:
  ccl::PrincipledBsdfNode *m_bsdf{nullptr};
  std::string m_colorMode;
  float3 m_color{make_float3(0.8f, 0.8f, 0.8f)};
  std::string m_opacityMode;
  float m_opacity{1.f};
  std::string m_roughnessMode;
  float m_roughness{1.f};
  std::string m_metallicMode;
  float m_metallic{0.f};
  float m_ior{1.5f};
};

PhysicallyBasedMaterial::PhysicallyBasedMaterial(CyclesGlobalState *s)
    : Material(s)
{
  auto *bsdf = m_graph->create_node<ccl::PrincipledBsdfNode>();
  m_graph->connect(bsdf->output("BSDF"), m_graph->output()->input("Surface"));
  m_bsdf = bsdf;
}

void PhysicallyBasedMaterial::commitParameters()
{
  m_colorMode = getParamString("baseColor", "");
  m_color = getParam<float3>("baseColor", make_float3(1.f, 1.f, 1.f));

  m_opacityMode = getParamString("opacity", "");
  m_opacity = getParam<float>("opacity", 1.f);

  m_roughnessMode = getParamString("roughness", "");
  m_roughness = getParam<float>("roughness", 1.f);

  m_metallicMode = getParamString("metallic", "");
  m_metallic = getParam<float>("metallic", 1.f);

  m_ior = getParam<float>("ior", 1.5f);
}

void PhysicallyBasedMaterial::finalize()
{
  connectAttributes(m_bsdf, m_colorMode, "Base Color", m_color, false);
  connectAttributes(m_bsdf, m_opacityMode, "Alpha", make_float3(m_opacity));
#if 0
  auto specularMode = getParamString("specular", "");
  auto specular = getParam<float>("specular", 0.f);
  connectAttributes(m_bsdf, specularMode, "Specular", make_float3(specular));
#endif
  connectAttributes(
      m_bsdf, m_roughnessMode, "Roughness", make_float3(m_roughness));
  connectAttributes(
      m_bsdf, m_metallicMode, "Metallic", make_float3(m_metallic));
#if 0
  auto transmissionMode = getParamString("transmission", "");
  auto transmission = getParam<float>("transmission", 0.f);
  connectAttributes(m_bsdf, transmissionMode, "Transmission", make_float3(transmission));

  auto clearcoatMode = getParamString("clearcoat", "");
  auto clearcoat = getParam<float>("clearcoat", 0.f);
  connectAttributes(m_bsdf, clearcoatMode, "Clearcoat", make_float3(clearcoat));

  auto clearcoatRoughnessMode = getParamString("clearcoatRoughness", "");
  auto clearcoatRoughness = getParam<float>("clearcoatRoughness", 0.f);
  connectAttributes(
      m_bsdf, clearcoatRoughnessMode, "Clearcoat Roughness", make_float3(clearcoatRoughness));
#endif
  m_bsdf->input("IOR")->set(m_ior);

  m_shader->tag_update(deviceState()->scene);
}

// Material definitions ///////////////////////////////////////////////////////

Material::Material(CyclesGlobalState *s) : Object(ANARI_SURFACE, s)
{
  auto &state = *deviceState();

  m_shader = state.scene->create_node<ccl::Shader>();

  auto graph = std::make_unique<ccl::ShaderGraph>();
  m_graph = graph.get();

  auto *vertexColor = m_graph->create_node<ccl::AttributeNode>();
  vertexColor->set_attribute(ccl::ustring("vertex.color"));

  auto *attr0 = m_graph->create_node<ccl::AttributeNode>();
  attr0->set_attribute(ccl::ustring("vertex.attribute0"));

  auto *attr1 = m_graph->create_node<ccl::AttributeNode>();
  attr1->set_attribute(ccl::ustring("vertex.attribute1"));

  auto *attr2 = m_graph->create_node<ccl::AttributeNode>();
  attr2->set_attribute(ccl::ustring("vertex.attribute2"));

  auto *attr3 = m_graph->create_node<ccl::AttributeNode>();
  attr3->set_attribute(ccl::ustring("vertex.attribute3"));

  auto *vertexColor_sc = m_graph->create_node<ccl::SeparateColorNode>();
  m_graph->connect(
      vertexColor->output("Color"), vertexColor_sc->input("Color"));

  auto *attr0_sc = m_graph->create_node<ccl::SeparateColorNode>();
  m_graph->connect(attr0->output("Color"), attr0_sc->input("Color"));

  auto *attr1_sc = m_graph->create_node<ccl::SeparateColorNode>();
  m_graph->connect(attr1->output("Color"), attr1_sc->input("Color"));

  auto *attr2_sc = m_graph->create_node<ccl::SeparateColorNode>();
  m_graph->connect(attr2->output("Color"), attr2_sc->input("Color"));

  auto *attr3_sc = m_graph->create_node<ccl::SeparateColorNode>();
  m_graph->connect(attr3->output("Color"), attr3_sc->input("Color"));

  m_shader->set_graph(std::move(graph));

  m_attributeNodes.attrC = vertexColor;
  m_attributeNodes.attr0 = attr0;
  m_attributeNodes.attr1 = attr1;
  m_attributeNodes.attr2 = attr2;
  m_attributeNodes.attr3 = attr3;
  m_attributeNodes.attrC_sc = vertexColor_sc;
  m_attributeNodes.attr0_sc = attr0_sc;
  m_attributeNodes.attr1_sc = attr1_sc;
  m_attributeNodes.attr2_sc = attr2_sc;
  m_attributeNodes.attr3_sc = attr3_sc;
}

Material::~Material()
{
  auto &state = *deviceState();
  state.scene->shaders.erase(cyclesShader());
}

Material *Material::createInstance(std::string_view type, CyclesGlobalState *s)
{
  if (type == "matte")
    return new MatteMaterial(s);
  else if (type == "physicallyBased")
    return new PhysicallyBasedMaterial(s);
  else
    return (Material *)new UnknownObject(ANARI_MATERIAL, type, s);
}

void Material::connectAttributes(ccl::ShaderNode *bsdf,
    const std::string &mode,
    const char *input,
    const float3 &v,
    bool singleComponent)
{
  if (mode == "color") {
    m_graph->connect(singleComponent ? m_attributeNodes.attrC_sc->output("Red")
                                     : m_attributeNodes.attrC->output("Color"),
        bsdf->input(input));
  } else if (mode == "attribute0") {
    m_graph->connect(singleComponent ? m_attributeNodes.attr0_sc->output("Red")
                                     : m_attributeNodes.attr0->output("Color"),
        bsdf->input(input));
  } else if (mode == "attribute1") {
    m_graph->connect(singleComponent ? m_attributeNodes.attr1_sc->output("Red")
                                     : m_attributeNodes.attr1->output("Color"),
        bsdf->input(input));
  } else if (mode == "attribute2") {
    m_graph->connect(singleComponent ? m_attributeNodes.attr2_sc->output("Red")
                                     : m_attributeNodes.attr2->output("Color"),
        bsdf->input(input));
  } else if (mode == "attribute3") {
    m_graph->connect(singleComponent ? m_attributeNodes.attr3_sc->output("Red")
                                     : m_attributeNodes.attr3->output("Color"),
        bsdf->input(input));
  } else {
    m_graph->disconnect(bsdf->input(input));
    if (singleComponent)
      bsdf->input(input)->set(v.x);
    else
      bsdf->input(input)->set(v);
  }
}

ccl::Shader *Material::cyclesShader()
{
  return m_shader;
}

} // namespace cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(cycles::Material *);
