// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Material.h"
// subtypes
#include "HairMaterial.h"
#include "MatteMaterial.h"
#include "OSLMaterial.h"
#include "PhysicallyBasedMaterial.h"
#include "ToonMaterial.h"
// std
#include <algorithm>
#include <cmath>

namespace anari_cycles {

// Helper functions ///////////////////////////////////////////////////////////

static bool isAttributeSource(const std::string &attributeSource)
{
  return attributeSource == "color" || attributeSource == "attribute0"
      || attributeSource == "attribute1" || attributeSource == "attribute2"
      || attributeSource == "attribute3" || attributeSource == "primitiveId";
}

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
  else if (type == "toon")
    return new ToonMaterial(s);
  else if (type == "hair")
    return new HairMaterial(s);
  else if (type == "osl")
    return new OSLMaterial(s);
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

bool Material::hasInteriorVolume() const
{
  return m_hasInteriorVolume;
}

void Material::makeGraph()
{
  m_samplerOutputs.clear();
  m_hasInteriorVolume = false;

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

  // 'primitiveId' is uploaded by every geometry as a per-primitive float
  // attribute (see writePrimitiveId() in geometry/GeometryAttributes.h).
  auto *attrPid = m_graph->create_node<ccl::AttributeNode>();
  attrPid->name = "attrPid";
  attrPid->set_attribute(ccl::ustring("primitiveId"));

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

  // Frame channels 'primitiveId'/'instanceId' (KHR_FRAME_CHANNEL_*): every
  // surface material writes two value AOVs that back the equally named
  // PASS_AOV_VALUE passes created at device init. Both ids are stored
  // biased by +1 so untouched pixels (background, volumes -- Cycles does
  // not run AOV nodes in volume shading) read back 0, which the frame
  // extraction maps to ~0u ("no id"); see
  // FrameOutputDriver::extractAovIdPass().
  auto *pidBias = m_graph->create_node<ccl::MathNode>();
  pidBias->name = "pidBias";
  pidBias->set_math_type(ccl::NODE_MATH_ADD);
  m_graph->connect(attrPid->output("Fac"), pidBias->input("Value1"));
  pidBias->input("Value2")->set(1.f);

  auto *aovPid = m_graph->create_node<ccl::OutputAOVNode>();
  aovPid->set_name(ccl::ustring("primitiveId"));
  m_graph->connect(pidBias->output("Value"), aovPid->input("Value"));

  // 'instanceId' is a per-object (not per-geometry) float attribute set in
  // Group::addGroupToCurrentCyclesScene(), already stored biased by +1.
  auto *attrIid = m_graph->create_node<ccl::AttributeNode>();
  attrIid->name = "attrIid";
  attrIid->set_attribute(ccl::ustring("instanceId"));

  auto *aovIid = m_graph->create_node<ccl::OutputAOVNode>();
  aovIid->set_name(ccl::ustring("instanceId"));
  m_graph->connect(attrIid->output("Fac"), aovIid->input("Value"));

  m_attributeNodes.attrC = vertexColor->output("Color");
  m_attributeNodes.attr0 = attr0->output("Color");
  m_attributeNodes.attr1 = attr1->output("Color");
  m_attributeNodes.attr2 = attr2->output("Color");
  m_attributeNodes.attr3 = attr3->output("Color");
  m_attributeNodes.attrPid = attrPid->output("Color");
  m_attributeNodes.attrC_sc = vertexColor_sc->output("Red");
  m_attributeNodes.attr0_sc = attr0_sc->output("Red");
  m_attributeNodes.attr1_sc = attr1_sc->output("Red");
  m_attributeNodes.attr2_sc = attr2_sc->output("Red");
  m_attributeNodes.attr3_sc = attr3_sc->output("Red");
  m_attributeNodes.attrPid_sc = attrPid->output("Fac");
  m_attributeNodes.attrC_a = vertexColor->output("Alpha");
  m_attributeNodes.attr0_a = attr0->output("Alpha");
  m_attributeNodes.attr1_a = attr1->output("Alpha");
  m_attributeNodes.attr2_a = attr2->output("Alpha");
  m_attributeNodes.attr3_a = attr3->output("Alpha");
}

// The 4th (alpha) component of an attribute source, or nullptr when it is
// the constant 1 (constant colors, scalar attributes like primitiveId).
ccl::ShaderOutput *Material::attributeAlphaOutput(
    const std::string &attributeSource)
{
  if (attributeSource == "color")
    return m_attributeNodes.attrC_a;
  if (attributeSource == "attribute0")
    return m_attributeNodes.attr0_a;
  if (attributeSource == "attribute1")
    return m_attributeNodes.attr1_a;
  if (attributeSource == "attribute2")
    return m_attributeNodes.attr2_a;
  if (attributeSource == "attribute3")
    return m_attributeNodes.attr3_a;
  return nullptr;
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
    const std::string &opacityAttribute,
    float opacity,
    Sampler *opacitySampler,
    const std::string &colorAttribute,
    Sampler *colorSampler,
    helium::AlphaMode mode,
    float cutoff)
{
  if (mode == helium::AlphaMode::OPAQUE) {
    connectAttributes(bsdf, "", "Alpha", 1.f);
    return;
  }

  // Alpha (4th) component of the color source; nullptr when it is the
  // constant 1 (verified against helide: alpha = color.w * opacity before
  // the alphaMode/alphaCutoff adjustment).
  ccl::ShaderOutput *colorAlpha = nullptr;
  if (colorSampler)
    colorAlpha = getSamplerOutputs(colorSampler).alphaOutput;
  else
    colorAlpha = attributeAlphaOutput(colorAttribute);

  const bool opacityDynamic =
      opacitySampler || isAttributeSource(opacityAttribute);

  if (!opacityDynamic && !colorAlpha) { // fully constant
    const float a = mode == helium::AlphaMode::MASK
        ? (opacity >= cutoff ? 1.f : 0.f)
        : opacity;
    connectAttributes(bsdf, "", "Alpha", a);
    return;
  }

  // alpha = opacity * colorAlpha as graph nodes
  ccl::ShaderOutput *alphaOut = nullptr;
  if (opacityDynamic || opacity != 1.f) {
    auto *mult = m_graph->create_node<ccl::MathNode>();
    mult->set_math_type(ccl::NODE_MATH_MULTIPLY);
    connectAttributes(mult, opacityAttribute, "Value1", opacity, opacitySampler);
    if (colorAlpha)
      m_graph->connect(colorAlpha, mult->input("Value2"));
    else
      mult->input("Value2")->set(1.f);
    alphaOut = mult->output("Value");
  } else {
    alphaOut = colorAlpha; // opacity is the constant 1
  }

  if (mode == helium::AlphaMode::MASK) {
    // Threshold in the graph, matching the constant branch above (keep when
    // alpha >= cutoff): less_than(alpha, cutoff) marks discards, then
    // 1 - that keeps the rest.
    auto *discard = m_graph->create_node<ccl::MathNode>();
    discard->set_math_type(ccl::NODE_MATH_LESS_THAN);
    m_graph->connect(alphaOut, discard->input("Value1"));
    discard->input("Value2")->set(cutoff);
    auto *keep = m_graph->create_node<ccl::MathNode>();
    keep->set_math_type(ccl::NODE_MATH_SUBTRACT);
    keep->input("Value1")->set(1.f);
    m_graph->connect(discard->output("Value"), keep->input("Value2"));
    m_graph->connect(keep->output("Value"), bsdf->input("Alpha"));
    return;
  }

  // AlphaMode::BLEND
  auto *shaderInput = bsdf->input("Alpha");
  if (shaderInput->link)
    m_graph->disconnect(shaderInput);
  m_graph->connect(alphaOut, shaderInput);
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
  auto outputs = sampler->createNodeGraph(m_graph);

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
  } else if (attributeSource == "primitiveId") {
    m_graph->connect(singleComponent ? m_attributeNodes.attrPid_sc
                                     : m_attributeNodes.attrPid,
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
