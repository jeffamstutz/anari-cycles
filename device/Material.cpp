// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Material.h"

namespace anari_cycles {

// MatteMaterial definitions //////////////////////////////////////////////////

struct MatteMaterial : public Material
{
  MatteMaterial(CyclesGlobalState *s);
  ~MatteMaterial() override = default;

  void commitParameters() override;
  void finalize() override;

 private:
  void makeGraph() override;

  ccl::PrincipledBsdfNode *m_bsdf{nullptr};
  std::string m_colorAttr;
  float3 m_color{make_float3(0.8f, 0.8f, 0.8f)};
  std::string m_opacityAttr;
  float m_opacity{1.f};
};

MatteMaterial::MatteMaterial(CyclesGlobalState *s) : Material(s) {}

void MatteMaterial::commitParameters()
{
  m_colorAttr = getParamString("color", "");
  m_color = getParam<float3>("color", make_float3(0.8f, 0.8f, 0.8f));
  m_opacityAttr = getParamString("opacity", "");
  m_opacity = getParam<float>("opacity", 1.f);
}

void MatteMaterial::finalize()
{
  makeGraph();
  connectAttributes(m_bsdf, m_colorAttr, "Base Color", m_color);
  connectAttributes(m_bsdf, m_opacityAttr, "Alpha", m_opacity);
  m_shader->tag_update(deviceState()->scene);
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

// PhysicallyBasedMaterial ////////////////////////////////////////////////////

struct PhysicallyBasedMaterial : public Material
{
  PhysicallyBasedMaterial(CyclesGlobalState *s);
  ~PhysicallyBasedMaterial() override = default;

  void commitParameters() override;
  void finalize() override;

 private:
  void makeGraph() override;

  ccl::PrincipledBsdfNode *m_bsdf{nullptr};
  std::string m_colorAttr;
  float3 m_color{make_float3(0.8f, 0.8f, 0.8f)};
  std::string m_opacityAttr;
  float m_opacity{1.f};
  std::string m_roughnessAttr;
  float m_roughness{1.f};
  std::string m_metallicAttr;
  float m_metallic{0.f};
  std::string m_clearcoatAttr;
  float m_clearcoat{0.f};
  std::string m_clearcoatRoughnessAttr;
  float m_clearcoatRoughness{0.f};
  std::string m_emissiveAttr;
  float3 m_emissive{0.f};
  std::string m_transmissionAttr;
  float m_transmission{0.f};
  float m_ior{1.5f};
};

PhysicallyBasedMaterial::PhysicallyBasedMaterial(CyclesGlobalState *s)
    : Material(s)
{}

void PhysicallyBasedMaterial::commitParameters()
{
  m_colorAttr = getParamString("baseColor", "");
  m_color = getParam<float3>("baseColor", make_float3(0.8f, 0.8f, 0.8f));

  m_opacityAttr = getParamString("opacity", "");
  m_opacity = getParam<float>("opacity", 1.f);

  m_roughnessAttr = getParamString("roughness", "");
  m_roughness = getParam<float>("roughness", 1.f);

  m_metallicAttr = getParamString("metallic", "");
  m_metallic = getParam<float>("metallic", 1.f);

  m_clearcoatAttr = getParamString("clearcoat", "");
  m_clearcoat = getParam<float>("clearcoat", 0.f);

  m_clearcoatRoughnessAttr = getParamString("clearcoatRoughness", "");
  m_clearcoatRoughness = getParam<float>("clearcoatRoughness", 0.f);

  m_emissiveAttr = getParamString("emissive", "");
  m_emissive = getParam<float3>("emissive", zero_float3());

  m_transmissionAttr = getParamString("transmission", "");
  m_transmission = getParam<float>("transmission", 0.f);
  m_ior = getParam<float>("ior", 1.5f);
}

void PhysicallyBasedMaterial::finalize()
{
  makeGraph();
  connectAttributes(m_bsdf, m_colorAttr, "Base Color", m_color);
  connectAttributes(m_bsdf, m_opacityAttr, "Alpha", m_opacity);
  connectAttributes(m_bsdf, m_roughnessAttr, "Roughness", m_roughness);
  connectAttributes(m_bsdf, m_metallicAttr, "Metallic", m_metallic);
  connectAttributes(m_bsdf, m_clearcoatAttr, "Coat Weight", m_clearcoat);
  connectAttributes(
      m_bsdf, m_clearcoatRoughnessAttr, "Coat Roughness", m_clearcoatRoughness);
  connectAttributes(m_bsdf, m_emissiveAttr, "Emission Color", m_emissive);
  connectAttributes(
      m_bsdf, m_transmissionAttr, "Transmission Weight", m_transmission);
  m_bsdf->input("IOR")->set(m_ior);

  m_shader->tag_update(deviceState()->scene);

  Material::finalize();
}

void PhysicallyBasedMaterial::makeGraph()
{
  Material::makeGraph();
  m_bsdf = m_graph->create_node<ccl::PrincipledBsdfNode>();
  m_graph->connect(m_bsdf->output("BSDF"), m_graph->output()->input("Surface"));
  m_bsdf->input("Emission Strength")->set(1.f);
}

// Material definitions ///////////////////////////////////////////////////////

Material::Material(CyclesGlobalState *s) : Object(ANARI_MATERIAL, s)
{
  m_shader = s->scene->create_node<ccl::Shader>();
  // makeGraph();
}

Material::~Material()
{
  deviceState()->scene->delete_node(cyclesShader());
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

void Material::finalize()
{
  Object::finalize();
}

ccl::Shader *Material::cyclesShader()
{
  return m_shader;
}

void Material::makeGraph()
{
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

  m_attributeNodes.attrC = vertexColor->output("Color");
  m_attributeNodes.attr0 = attr0->output("Color");
  m_attributeNodes.attr1 = attr1->output("Color");
  m_attributeNodes.attr2 = attr2->output("Color");
  m_attributeNodes.attr3 = attr3->output("Color");
  m_attributeNodes.attrC_sc = vertexColor_sc->output("Red");
  m_attributeNodes.attr0_sc = attr0_sc->output("Red");
  m_attributeNodes.attr1_sc = attr1_sc->output("Red");
  m_attributeNodes.attr2_sc = attr2_sc->output("Red");
  m_attributeNodes.attr3_sc = attr3_sc->output("Red");
}

void Material::connectAttributes(
    ccl::ShaderNode *bsdf, const std::string &mode, const char *input, float v)
{
  connectAttributesImpl(bsdf, mode, input, make_float3(v), true);
}

void Material::connectAttributes(ccl::ShaderNode *bsdf,
    const std::string &mode,
    const char *input,
    const float3 &v)
{
  connectAttributesImpl(bsdf, mode, input, v, false);
}

void Material::connectAttributesImpl(ccl::ShaderNode *bsdf,
    const std::string &mode,
    const char *input,
    const float3 &v,
    bool singleComponent)
{
  auto *shaderInput = bsdf->input(input);
  m_graph->disconnect(shaderInput);

  if (mode == "color") {
    m_graph->connect(
        singleComponent ? m_attributeNodes.attrC_sc : m_attributeNodes.attrC,
        shaderInput);
  } else if (mode == "attribute0") {
    m_graph->connect(
        singleComponent ? m_attributeNodes.attr0_sc : m_attributeNodes.attr0,
        shaderInput);
  } else if (mode == "attribute1") {
    m_graph->connect(
        singleComponent ? m_attributeNodes.attr1_sc : m_attributeNodes.attr1,
        shaderInput);
  } else if (mode == "attribute2") {
    m_graph->connect(
        singleComponent ? m_attributeNodes.attr2_sc : m_attributeNodes.attr2,
        shaderInput);
  } else if (mode == "attribute3") {
    m_graph->connect(
        singleComponent ? m_attributeNodes.attr3_sc : m_attributeNodes.attr3,
        shaderInput);
  } else {
    if (singleComponent)
      shaderInput->set(v.x);
    else
      shaderInput->set(v);
  }
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Material *);
