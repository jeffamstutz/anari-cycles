// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Material.h"
#include "Sampler.h"

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
  helium::ChangeObserverPtr<Sampler> m_colorSampler;

  std::string m_opacityAttr;
  float m_opacity{1.f};
  helium::ChangeObserverPtr<Sampler> m_opacitySampler;

  helium::AlphaMode m_mode{helium::AlphaMode::OPAQUE};
};

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
}

void MatteMaterial::finalize()
{
  auto &state = *deviceState();

  makeGraph();

  connectAttributes(m_bsdf,
      m_colorAttr,
      "Base Color",
      m_color,
      m_colorSampler.get());

  const bool isOpaque = m_mode == helium::AlphaMode::OPAQUE;
  connectAttributes(m_bsdf,
      m_opacityAttr,
      "Alpha",
      isOpaque ? 1.f : m_opacity,
      m_opacitySampler && !isOpaque ? m_opacitySampler.get() : nullptr);

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
  helium::ChangeObserverPtr<Sampler> m_colorSampler;

  std::string m_opacityAttr;
  float m_opacity{1.f};
  helium::ChangeObserverPtr<Sampler> m_opacitySampler;

  std::string m_roughnessAttr;
  float m_roughness{1.f};
  helium::ChangeObserverPtr<Sampler> m_roughnessSampler;

  std::string m_metallicAttr;
  float m_metallic{0.f};
  helium::ChangeObserverPtr<Sampler> m_metallicSampler;

  helium::ChangeObserverPtr<Sampler> m_normalSampler;

  std::string m_clearcoatAttr;
  float m_clearcoat{0.f};
  std::string m_clearcoatRoughnessAttr;
  float m_clearcoatRoughness{0.f};
  std::string m_emissiveAttr;
  float3 m_emissive{0.f};
  std::string m_transmissionAttr;
  float m_transmission{0.f};
  float m_ior{1.5f};

  helium::AlphaMode m_mode{helium::AlphaMode::OPAQUE};
};

PhysicallyBasedMaterial::PhysicallyBasedMaterial(CyclesGlobalState *s)
    : Material(s),
      m_colorSampler(this),
      m_opacitySampler(this),
      m_roughnessSampler(this),
      m_metallicSampler(this),
      m_normalSampler(this)
{}

void PhysicallyBasedMaterial::commitParameters()
{
  m_colorAttr = getParamString("baseColor", "");
  m_color = getParam<float3>("baseColor", make_float3(0.8f, 0.8f, 0.8f));
  m_colorSampler = getParamObject<Sampler>("baseColor");

  m_opacityAttr = getParamString("opacity", "");
  m_opacity = getParam<float>("opacity", 1.f);
  m_opacitySampler = getParamObject<Sampler>("opacity");

  m_roughnessAttr = getParamString("roughness", "");
  m_roughness = getParam<float>("roughness", 1.f);
  m_roughnessSampler = getParamObject<Sampler>("roughness");

  m_metallicAttr = getParamString("metallic", "");
  m_metallic = getParam<float>("metallic", 1.f);
  m_metallicSampler = getParamObject<Sampler>("metallic");

  m_clearcoatAttr = getParamString("clearcoat", "");
  m_clearcoat = getParam<float>("clearcoat", 0.f);

  m_clearcoatRoughnessAttr = getParamString("clearcoatRoughness", "");
  m_clearcoatRoughness = getParam<float>("clearcoatRoughness", 0.f);

  m_emissiveAttr = getParamString("emissive", "");
  m_emissive = getParam<float3>("emissive", zero_float3());

  m_transmissionAttr = getParamString("transmission", "");
  m_transmission = getParam<float>("transmission", 0.f);
  m_ior = getParam<float>("ior", 1.5f);

  m_normalSampler = getParamObject<Sampler>("normal");

  m_mode = helium::alphaModeFromString(getParamString("alphaMode", "opaque"));
}

void PhysicallyBasedMaterial::finalize()
{
  auto &state = *deviceState();

  makeGraph();

  connectAttributes(m_bsdf,
      m_colorAttr,
      "Base Color",
      m_color,
      m_colorSampler.get());

  const bool isOpaque = m_mode == helium::AlphaMode::OPAQUE;
  connectAttributes(m_bsdf,
      m_opacityAttr,
      "Alpha",
      isOpaque ? 1.f : m_opacity,
      m_opacitySampler && !isOpaque ? m_opacitySampler.get() : nullptr);

  connectAttributes(m_bsdf,
      m_roughnessAttr,
      "Roughness",
      m_roughness,
      m_roughnessSampler.get());

  connectAttributes(m_bsdf,
      m_metallicAttr,
      "Metallic",
      m_metallic,
      m_metallicSampler.get());

  connectAttributes(m_bsdf, m_clearcoatAttr, "Coat Weight", m_clearcoat);
  connectAttributes(
      m_bsdf, m_clearcoatRoughnessAttr, "Coat Roughness", m_clearcoatRoughness);
  connectAttributes(m_bsdf, m_emissiveAttr, "Emission Color", m_emissive);
  connectAttributes(
      m_bsdf, m_transmissionAttr, "Transmission Weight", m_transmission);
  m_bsdf->input("IOR")->set(m_ior);

  if (m_normalSampler) {
    // Does not work yet, most probably need to figure out tangent space handling in Cycles
    //
    // m_graph->connect(getSamplerOutputs(m_normalSampler.get()).normalOutput,
    //    m_bsdf->input("Normal"));
  }

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
  m_samplerOutputs.clear();

  auto graph = std::make_unique<ccl::ShaderGraph>();
  m_graph = graph.get();

  auto *vertexColor = m_graph->create_node<ccl::AttributeNode>();
  vertexColor->name = "vertexColor";
  vertexColor->set_attribute(ccl::ustring("vertex.color"));

  auto *attr0 = m_graph->create_node<ccl::AttributeNode>();
  attr0->name = "attr0";
  attr0->set_attribute(ccl::ustring("vertex.attribute0"));

  auto *attr1 = m_graph->create_node<ccl::AttributeNode>();
  attr1->name = "attr1";
  attr1->set_attribute(ccl::ustring("vertex.attribute1"));

  auto *attr2 = m_graph->create_node<ccl::AttributeNode>();
  attr2->name = "attr2";
  attr2->set_attribute(ccl::ustring("vertex.attribute2"));

  auto *attr3 = m_graph->create_node<ccl::AttributeNode>();
  attr3->name = "attr3";
  attr3->set_attribute(ccl::ustring("vertex.attribute3"));

  auto *vertexColor_sc = m_graph->create_node<ccl::SeparateColorNode>();
  m_graph->connect(vertexColor->output("Color"), vertexColor_sc->input("Color"));

  auto *attr0_sc = m_graph->create_node<ccl::SeparateColorNode>();
  m_graph->connect(attr0->output("Color"), attr0_sc->input("Color"));

  auto *attr1_sc = m_graph->create_node<ccl::SeparateColorNode>();
  m_graph->connect(attr1->output("Color"), attr1_sc->input("Color"));

  auto *attr2_sc = m_graph->create_node<ccl::SeparateColorNode>();
  m_graph->connect(attr2->output("Color"), attr2_sc->input("Color"));

  auto *attr3_sc = m_graph->create_node<ccl::SeparateColorNode>();
  m_graph->connect(attr3->output("Color"), attr3_sc->input("Color"));

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

  m_shader->set_graph(std::move(graph));
}

void Material::connectAttributes(ccl::ShaderNode *bsdf,
    const std::string &attributeSource,
    const char *input,
    float v,
    Sampler *sampler)
{
  connectAttributesImpl(bsdf,
      attributeSource,
      sampler,
      input,
      make_float3(v),
      true);
}

void Material::connectAttributes(ccl::ShaderNode *bsdf,
    const std::string &attributeSource,
    const char *input,
    const float3 &v,
    Sampler *sampler)
{
  connectAttributesImpl(
      bsdf, attributeSource, sampler, input, v, false);
}

Sampler::SamplerOutputs Material::getSamplerOutputs(Sampler *sampler)
{
  if (!sampler) {
    return Sampler::SamplerOutputs{};
  }
  
  auto it = m_samplerOutputs.find(sampler);
  if (it != m_samplerOutputs.end() && it->second.isValid) {
    return it->second.outputs;
  }
  
  // Create new outputs using the sampler's node graph
  auto outputs = sampler->createNodeGraph(m_graph, m_attributeNodes.attr0);
  
  // Cache the outputs
  m_samplerOutputs[sampler] = {outputs, true};
  
  return outputs;
}

void Material::connectAttributesImpl(ccl::ShaderNode *bsdf,
    const std::string &attributeSource,
    Sampler *sampler,
    const char *input,
    const float3 &v,
    bool singleComponent)
{
  auto *shaderInput = bsdf->input(input);
  if (shaderInput->link)
    m_graph->disconnect(shaderInput);

  if (sampler) {
    // Get or create sampler outputs
    auto samplerOutputs = getSamplerOutputs(sampler);
    
    // Choose the appropriate output based on what we need
    ccl::ShaderOutput *outputToConnect = nullptr;
    if (singleComponent && samplerOutputs.scalarOutput) {
      outputToConnect = samplerOutputs.scalarOutput;
    } else if (!singleComponent && samplerOutputs.colorOutput) {
      outputToConnect = samplerOutputs.colorOutput;
    } else if (samplerOutputs.colorOutput) {
      // Fallback to color output if scalar not available
      outputToConnect = samplerOutputs.colorOutput;
    }
    
    if (outputToConnect) {
      m_graph->connect(outputToConnect, shaderInput);
      return;
    }
  }
  
  // Handle attribute connections
  if (attributeSource == "color") {
    m_graph->connect(
        singleComponent ? m_attributeNodes.attrC_sc : m_attributeNodes.attrC,
        shaderInput);
  } else if (attributeSource == "attribute0") {
    m_graph->connect(
        singleComponent ? m_attributeNodes.attr0_sc : m_attributeNodes.attr0,
        shaderInput);
  } else if (attributeSource == "attribute1") {
    m_graph->connect(
        singleComponent ? m_attributeNodes.attr1_sc : m_attributeNodes.attr1,
        shaderInput);
  } else if (attributeSource == "attribute2") {
    m_graph->connect(
        singleComponent ? m_attributeNodes.attr2_sc : m_attributeNodes.attr2,
        shaderInput);
  } else if (attributeSource == "attribute3") {
    m_graph->connect(
        singleComponent ? m_attributeNodes.attr3_sc : m_attributeNodes.attr3,
        shaderInput);
  } else {
    // Use constant value
    if (singleComponent)
      shaderInput->set(v.x);
    else
      shaderInput->set(v);
  }
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Material *);
