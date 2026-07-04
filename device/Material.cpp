// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Material.h"
#include "Sampler.h"
// std
#include <algorithm>
#include <cmath>

namespace anari_cycles {

// Helper functions ///////////////////////////////////////////////////////////

static bool isAttributeSource(const std::string &attributeSource)
{
  return attributeSource == "color" || attributeSource == "attribute0"
      || attributeSource == "attribute1" || attributeSource == "attribute2"
      || attributeSource == "attribute3";
}

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
  float m_alphaCutoff{0.5f};
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
  float3 m_color{make_float3(1.f, 1.f, 1.f)};
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
  helium::ChangeObserverPtr<Sampler> m_clearcoatSampler;
  std::string m_clearcoatRoughnessAttr;
  float m_clearcoatRoughness{0.f};
  helium::ChangeObserverPtr<Sampler> m_clearcoatRoughnessSampler;
  helium::ChangeObserverPtr<Sampler> m_clearcoatNormalSampler;
  std::string m_emissiveAttr;
  float3 m_emissive{make_float3(0.f)};
  helium::ChangeObserverPtr<Sampler> m_emissiveSampler;
  std::string m_transmissionAttr;
  float m_transmission{0.f};
  helium::ChangeObserverPtr<Sampler> m_transmissionSampler;
  std::string m_iorAttr;
  float m_ior{1.5f};
  helium::ChangeObserverPtr<Sampler> m_iorSampler;

  std::string m_specularAttr;
  float m_specular{1.f};
  helium::ChangeObserverPtr<Sampler> m_specularSampler;
  std::string m_specularColorAttr;
  float3 m_specularColor{make_float3(1.f, 1.f, 1.f)};
  helium::ChangeObserverPtr<Sampler> m_specularColorSampler;

  std::string m_sheenColorAttr;
  float3 m_sheenColor{make_float3(0.f)};
  helium::ChangeObserverPtr<Sampler> m_sheenColorSampler;
  std::string m_sheenRoughnessAttr;
  float m_sheenRoughness{0.f};
  helium::ChangeObserverPtr<Sampler> m_sheenRoughnessSampler;

  float m_iridescence{0.f};
  float m_iridescenceIor{1.3f};
  float m_iridescenceThickness{0.f};

  float m_thickness{0.f};
  float3 m_attenuationColor{make_float3(1.f, 1.f, 1.f)};
  float m_attenuationDistance{INFINITY};

  helium::AlphaMode m_mode{helium::AlphaMode::OPAQUE};
  float m_alphaCutoff{0.5f};
  bool m_warnedOcclusion{false};
};

PhysicallyBasedMaterial::PhysicallyBasedMaterial(CyclesGlobalState *s)
    : Material(s),
      m_colorSampler(this),
      m_opacitySampler(this),
      m_roughnessSampler(this),
      m_metallicSampler(this),
      m_normalSampler(this),
      m_clearcoatSampler(this),
      m_clearcoatRoughnessSampler(this),
      m_clearcoatNormalSampler(this),
      m_emissiveSampler(this),
      m_transmissionSampler(this),
      m_iorSampler(this),
      m_specularSampler(this),
      m_specularColorSampler(this),
      m_sheenColorSampler(this),
      m_sheenRoughnessSampler(this)
{}

void PhysicallyBasedMaterial::commitParameters()
{
  m_colorAttr = getParamString("baseColor", "");
  m_color = getParam<float3>("baseColor", make_float3(1.f, 1.f, 1.f));
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
  m_clearcoatSampler = getParamObject<Sampler>("clearcoat");

  m_clearcoatRoughnessAttr = getParamString("clearcoatRoughness", "");
  m_clearcoatRoughness = getParam<float>("clearcoatRoughness", 0.f);
  m_clearcoatRoughnessSampler = getParamObject<Sampler>("clearcoatRoughness");

  m_clearcoatNormalSampler = getParamObject<Sampler>("clearcoatNormal");

  m_emissiveAttr = getParamString("emissive", "");
  m_emissive = getParam<float3>("emissive", zero_float3());
  m_emissiveSampler = getParamObject<Sampler>("emissive");

  m_transmissionAttr = getParamString("transmission", "");
  m_transmission = getParam<float>("transmission", 0.f);
  m_transmissionSampler = getParamObject<Sampler>("transmission");

  m_iorAttr = getParamString("ior", "");
  m_ior = getParam<float>("ior", 1.5f);
  m_iorSampler = getParamObject<Sampler>("ior");

  // NOTE: the ANARI registry lists 0.0 as the default for 'specular', but the
  // underlying glTF KHR_materials_specular extension (which this parameter
  // mirrors) defaults to 1.0 (i.e. full IOR-derived Fresnel reflection).
  // Defaulting to 0 would strip all specular reflection off every dielectric
  // that doesn't set the parameter, so we follow glTF here (documented in
  // device/json/cycles_khr_material_physically_based.json).
  m_specularAttr = getParamString("specular", "");
  m_specular = getParam<float>("specular", 1.f);
  m_specularSampler = getParamObject<Sampler>("specular");

  m_specularColorAttr = getParamString("specularColor", "");
  m_specularColor = getParam<float3>("specularColor", make_float3(1.f, 1.f, 1.f));
  m_specularColorSampler = getParamObject<Sampler>("specularColor");

  m_sheenColorAttr = getParamString("sheenColor", "");
  m_sheenColor = getParam<float3>("sheenColor", zero_float3());
  m_sheenColorSampler = getParamObject<Sampler>("sheenColor");

  m_sheenRoughnessAttr = getParamString("sheenRoughness", "");
  m_sheenRoughness = getParam<float>("sheenRoughness", 0.f);
  m_sheenRoughnessSampler = getParamObject<Sampler>("sheenRoughness");

  m_iridescence = getParam<float>("iridescence", 0.f);
  m_iridescenceIor = getParam<float>("iridescenceIor", 1.3f);
  m_iridescenceThickness = getParam<float>("iridescenceThickness", 0.f);

  m_thickness = getParam<float>("thickness", 0.f);
  m_attenuationColor =
      getParam<float3>("attenuationColor", make_float3(1.f, 1.f, 1.f));
  m_attenuationDistance = getParam<float>("attenuationDistance", INFINITY);

  m_normalSampler = getParamObject<Sampler>("normal");

  // 'occlusion' is a rasterizer-oriented baked-AO map; a path tracer computes
  // occlusion by actually tracing rays, and Cycles' Principled BSDF has no
  // socket for it. Ignore it (multiplying it into base color would darken
  // directly lit areas incorrectly).
  if (hasParam("occlusion")) {
    if (!m_warnedOcclusion) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "physicallyBased material parameter 'occlusion' is not supported by "
          "the cycles device and will be ignored");
      m_warnedOcclusion = true;
    }
  } else {
    m_warnedOcclusion = false; // warn again if it is set anew later
  }

  m_mode = helium::alphaModeFromString(getParamString("alphaMode", "opaque"));
  m_alphaCutoff = getParam<float>("alphaCutoff", 0.5f);
}

void PhysicallyBasedMaterial::finalize()
{
  makeGraph();

  connectAttributes(
      m_bsdf, m_colorAttr, "Base Color", m_color, m_colorSampler.get());

  connectAlpha(m_bsdf,
      m_opacityAttr,
      m_opacity,
      m_opacitySampler.get(),
      m_mode,
      m_alphaCutoff);

  connectAttributes(m_bsdf,
      m_roughnessAttr,
      "Roughness",
      m_roughness,
      m_roughnessSampler.get());

  connectAttributes(
      m_bsdf, m_metallicAttr, "Metallic", m_metallic, m_metallicSampler.get());

  connectAttributes(m_bsdf,
      m_clearcoatAttr,
      "Coat Weight",
      m_clearcoat,
      m_clearcoatSampler.get());
  connectAttributes(m_bsdf,
      m_clearcoatRoughnessAttr,
      "Coat Roughness",
      m_clearcoatRoughness,
      m_clearcoatRoughnessSampler.get());
  connectAttributes(m_bsdf,
      m_emissiveAttr,
      "Emission Color",
      m_emissive,
      m_emissiveSampler.get());
  connectAttributes(m_bsdf,
      m_transmissionAttr,
      "Transmission Weight",
      m_transmission,
      m_transmissionSampler.get());
  connectAttributes(m_bsdf, m_iorAttr, "IOR", m_ior, m_iorSampler.get());

  // ANARI 'specular' scales the dielectric F0; Cycles' "Specular IOR Level"
  // does the same but with a neutral value of 0.5 (IOR-derived Fresnel), so
  // scale by 0.5. Non-constant sources go through a multiply node.
  if (m_specularSampler || !m_specularAttr.empty()) {
    auto *scale = m_graph->create_node<ccl::MathNode>();
    scale->set_math_type(ccl::NODE_MATH_MULTIPLY);
    connectAttributes(
        scale, m_specularAttr, "Value1", m_specular, m_specularSampler.get());
    scale->input("Value2")->set(0.5f);
    m_graph->connect(
        scale->output("Value"), m_bsdf->input("Specular IOR Level"));
  } else {
    m_bsdf->input("Specular IOR Level")->set(0.5f * m_specular);
  }
  connectAttributes(m_bsdf,
      m_specularColorAttr,
      "Specular Tint",
      m_specularColor,
      m_specularColorSampler.get());

  // Cycles splits sheen into weight * tint; ANARI only has sheenColor, so
  // enable the sheen lobe whenever a color source is present (a black
  // constant tint contributes nothing either way).
  const bool sheenActive = m_sheenColorSampler || !m_sheenColorAttr.empty()
      || std::max({m_sheenColor.x, m_sheenColor.y, m_sheenColor.z}) > 0.f;
  m_bsdf->input("Sheen Weight")->set(sheenActive ? 1.f : 0.f);
  connectAttributes(m_bsdf,
      m_sheenColorAttr,
      "Sheen Tint",
      m_sheenColor,
      m_sheenColorSampler.get());
  connectAttributes(m_bsdf,
      m_sheenRoughnessAttr,
      "Sheen Roughness",
      m_sheenRoughness,
      m_sheenRoughnessSampler.get());

  // Cycles' thin-film has no separate weight input (glTF's iridescence factor
  // blends between the plain and the thin-film Fresnel response). Scaling the
  // thickness by the weight would shift the interference hue, so instead any
  // nonzero 'iridescence' enables the film at full strength: hue-correct, but
  // partial weights render stronger than the reference.
  m_bsdf->input("Thin Film Thickness")
      ->set(m_iridescence > 0.f ? m_iridescenceThickness : 0.f);
  m_bsdf->input("Thin Film IOR")->set(m_iridescenceIor);

  // thickness/attenuationColor/attenuationDistance -> interior absorption
  // volume (Beer-Lambert). sigma_t is chosen so that light traveling
  // 'attenuationDistance' through the interior is tinted 'attenuationColor'.
  // The scalar 'thickness' only gates the effect: Cycles integrates along the
  // actual interior path length of the closed geometry. Only wire the volume
  // when transmission can be nonzero -- connecting it unconditionally would
  // enable Cycles' volume kernels/stack for materials no ray can enter.
  const bool mayTransmit = m_transmission > 0.f || m_transmissionSampler
      || !m_transmissionAttr.empty();
  if (mayTransmit && m_thickness > 0.f && m_attenuationDistance > 0.f
      && std::isfinite(m_attenuationDistance)) {
    const float3 sigma = make_float3(
        -std::log(std::clamp(m_attenuationColor.x, 1e-4f, 1.f)),
        -std::log(std::clamp(m_attenuationColor.y, 1e-4f, 1.f)),
        -std::log(std::clamp(m_attenuationColor.z, 1e-4f, 1.f)))
        / m_attenuationDistance;
    const float density = std::max({sigma.x, sigma.y, sigma.z});
    if (density > 0.f) {
      auto *absorption = m_graph->create_node<ccl::AbsorptionVolumeNode>();
      // AbsorptionVolumeNode computes sigma_t = (1 - color) * density
      absorption->set_color(make_float3(1.f - sigma.x / density,
          1.f - sigma.y / density,
          1.f - sigma.z / density));
      absorption->set_density(density);
      m_graph->connect(
          absorption->output("Volume"), m_graph->output()->input("Volume"));
    }
  }

  if (m_normalSampler) {
    auto samplerOutputs = getSamplerOutputs(m_normalSampler.get());
    if (samplerOutputs.colorOutput) {
      auto *normalMap = m_graph->create_node<ccl::NormalMapNode>();
      normalMap->set_space(ccl::NODE_NORMAL_MAP_TANGENT);
      normalMap->set_attribute(ccl::ustring(""));
      m_graph->connect(samplerOutputs.colorOutput, normalMap->input("Color"));
      m_graph->connect(normalMap->output("Normal"), m_bsdf->input("Normal"));
    }
  }

  if (m_clearcoatNormalSampler) {
    auto samplerOutputs = getSamplerOutputs(m_clearcoatNormalSampler.get());
    if (samplerOutputs.colorOutput) {
      auto *normalMap = m_graph->create_node<ccl::NormalMapNode>();
      normalMap->set_space(ccl::NODE_NORMAL_MAP_TANGENT);
      normalMap->set_attribute(ccl::ustring(""));
      m_graph->connect(samplerOutputs.colorOutput, normalMap->input("Color"));
      m_graph->connect(
          normalMap->output("Normal"), m_bsdf->input("Coat Normal"));
    }
  }

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

Material::Material(CyclesGlobalState *s) : Object(ANARI_MATERIAL, s) {}

Material::~Material()
{
  // Object release can happen while the render thread reads the scene.
  CyclesGlobalState::SceneLock sceneLock(*deviceState());
  if (m_shader)
    deviceState()->scene->delete_node(m_shader);
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
  // Hand the completed graph to the shader only now: Shader::set_graph()
  // snapshots graph-derived state (e.g. has_volume_connected, which gates
  // KERNEL_FEATURE_VOLUME), so it must see the fully built graph.
  if (m_graphOwned)
    m_shader->set_graph(std::move(m_graphOwned));
  if (m_shader->graph)
    m_shader->tag_update(deviceState()->scene);
  Object::finalize();
}

bool Material::isValid() const
{
  return m_shader && m_graph;
}

ccl::Shader *Material::cyclesShader()
{
  return m_shader;
}

void Material::makeGraph()
{
  m_samplerOutputs.clear();

  if (!m_shader)
    m_shader = deviceState()->scene->create_node<ccl::Shader>();

  m_graphOwned = std::make_unique<ccl::ShaderGraph>();
  m_graph = m_graphOwned.get();

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

void Material::connectAttributes(ccl::ShaderNode *bsdf,
    const std::string &attributeSource,
    const char *input,
    float v,
    Sampler *sampler)
{
  connectAttributesImpl(
      bsdf, attributeSource, sampler, input, make_float3(v), true);
}

void Material::connectAttributes(ccl::ShaderNode *bsdf,
    const std::string &attributeSource,
    const char *input,
    const float3 &v,
    Sampler *sampler)
{
  connectAttributesImpl(bsdf, attributeSource, sampler, input, v, false);
}

void Material::connectAlpha(ccl::ShaderNode *bsdf,
    const std::string &attributeSource,
    float opacity,
    Sampler *sampler,
    helium::AlphaMode mode,
    float cutoff)
{
  if (mode == helium::AlphaMode::OPAQUE) {
    connectAttributes(bsdf, "", "Alpha", 1.f);
    return;
  }

  if (mode == helium::AlphaMode::MASK) {
    if (sampler || isAttributeSource(attributeSource)) {
      // Threshold non-constant opacity in the graph, exactly matching the
      // constant branch below (keep when alpha >= cutoff):
      // less_than(alpha, cutoff) marks discards, then 1 - that keeps the rest.
      auto *discard = m_graph->create_node<ccl::MathNode>();
      discard->set_math_type(ccl::NODE_MATH_LESS_THAN);
      connectAttributes(discard, attributeSource, "Value1", opacity, sampler);
      discard->input("Value2")->set(cutoff);
      auto *keep = m_graph->create_node<ccl::MathNode>();
      keep->set_math_type(ccl::NODE_MATH_SUBTRACT);
      keep->input("Value1")->set(1.f);
      m_graph->connect(discard->output("Value"), keep->input("Value2"));
      m_graph->connect(keep->output("Value"), bsdf->input("Alpha"));
    } else {
      connectAttributes(bsdf, "", "Alpha", opacity >= cutoff ? 1.f : 0.f);
    }
    return;
  }

  // AlphaMode::BLEND
  connectAttributes(bsdf, attributeSource, "Alpha", opacity, sampler);
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
