// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Sampler.h"
#include "SamplerUtils.h"
// subtypes
#include "Image1D.h"
#include "Image2D.h"
#include "PrimitiveSampler.h"
#include "TransformSampler.h"
#include "UnknownSampler.h"

#include "cycles_math.h"

namespace anari_cycles {

// Sampler base helpers ///////////////////////////////////////////////////////

Sampler::ColorAlpha Sampler::makeAttributeInput(
    ccl::ShaderGraph *graph, const std::string &attribute) const
{
  if (attribute == "primitiveId") {
    // Uploaded by every geometry as a per-primitive float attribute; a scalar
    // attribute expands to (v, 0, 0, 1) per the ANARI spec.
    auto *node = graph->create_node<ccl::AttributeNode>();
    node->set_attribute(ccl::ustring("primitiveId"));
    auto *combine = graph->create_node<ccl::CombineXYZNode>();
    graph->connect(node->output("Fac"), combine->input("X"));
    return {combine->output("Vector"), nullptr};
  }

  if (const char *name = cyclesAttributeName(attribute)) {
    auto *node = graph->create_node<ccl::AttributeNode>();
    node->set_attribute(ccl::ustring(name));
    return {node->output("Color"), node->output("Alpha")};
  }

  reportMessage(ANARI_SEVERITY_WARNING,
      "sampler 'inAttribute' value '%s' is not supported -- using (0,0,0,1)",
      attribute.c_str());
  auto *zero = graph->create_node<ccl::CombineXYZNode>();
  return {zero->output("Vector"), nullptr};
}

Sampler::ColorAlpha Sampler::applyAffineTransform(ccl::ShaderGraph *graph,
    const ColorAlpha &in,
    const mat4 &m,
    const helium::float4 &offset)
{
  // Column-major mat4: rows of the linear part gathered per output component.
  const float3 row0 = make_float3(m.x.x, m.y.x, m.z.x);
  const float3 row1 = make_float3(m.x.y, m.y.y, m.z.y);
  const float3 row2 = make_float3(m.x.z, m.y.z, m.z.z);
  const float3 rowA = make_float3(m.x.w, m.y.w, m.z.w); // alpha row
  const float3 wCol = make_float3(m.w.x, m.w.y, m.w.z); // 4th-column xyz
  const float aScale = m.w.w;

  const bool has3x3 = row0 != make_float3(1.f, 0.f, 0.f)
      || row1 != make_float3(0.f, 1.f, 0.f) || row2 != make_float3(0.f, 0.f, 1.f);
  const bool hasWCol = wCol != zero_float3();
  const bool hasAlphaRow =
      rowA != zero_float3() || aScale != 1.f || offset.w != 0.f;

  ColorAlpha out = in;

  // --- color: rgb' = M3x3 * rgb + a * wCol + offset.xyz
  if (has3x3) {
    auto dotRow = [&](const float3 &row) {
      auto *dot = graph->create_node<ccl::VectorMathNode>();
      dot->set_math_type(ccl::NODE_VECTOR_MATH_DOT_PRODUCT);
      graph->connect(in.color, dot->input("Vector1"));
      dot->set_vector2(row);
      return dot->output("Value");
    };
    auto *combine = graph->create_node<ccl::CombineXYZNode>();
    graph->connect(dotRow(row0), combine->input("X"));
    graph->connect(dotRow(row1), combine->input("Y"));
    graph->connect(dotRow(row2), combine->input("Z"));
    out.color = combine->output("Vector");
  }

  float3 constOffset = make_float3(offset.x, offset.y, offset.z);
  if (hasWCol) {
    if (in.alpha) {
      auto *scaled = graph->create_node<ccl::VectorMathNode>();
      scaled->set_math_type(ccl::NODE_VECTOR_MATH_SCALE);
      scaled->set_vector1(wCol);
      graph->connect(in.alpha, scaled->input("Scale"));
      auto *add = graph->create_node<ccl::VectorMathNode>();
      add->set_math_type(ccl::NODE_VECTOR_MATH_ADD);
      graph->connect(out.color, add->input("Vector1"));
      graph->connect(scaled->output("Vector"), add->input("Vector2"));
      out.color = add->output("Vector");
    } else {
      constOffset += wCol; // alpha is the constant 1
    }
  }

  if (constOffset != zero_float3()) {
    auto *add = graph->create_node<ccl::VectorMathNode>();
    add->set_math_type(ccl::NODE_VECTOR_MATH_ADD);
    graph->connect(out.color, add->input("Vector1"));
    add->set_vector2(constOffset);
    out.color = add->output("Vector");
  }

  // --- alpha: a' = dot(rgb, rowA) + a * aScale + offset.w
  if (hasAlphaRow) {
    ccl::ShaderOutput *acc = nullptr;
    float constPart = offset.w + (in.alpha ? 0.f : aScale);

    if (rowA != zero_float3()) {
      auto *dot = graph->create_node<ccl::VectorMathNode>();
      dot->set_math_type(ccl::NODE_VECTOR_MATH_DOT_PRODUCT);
      graph->connect(in.color, dot->input("Vector1"));
      dot->set_vector2(rowA);
      acc = dot->output("Value");
    }

    if (in.alpha && aScale != 0.f) {
      auto *mad = graph->create_node<ccl::MathNode>();
      mad->set_math_type(ccl::NODE_MATH_MULTIPLY_ADD);
      graph->connect(in.alpha, mad->input("Value1"));
      mad->input("Value2")->set(aScale);
      if (acc)
        graph->connect(acc, mad->input("Value3"));
      else {
        mad->input("Value3")->set(constPart);
        constPart = 0.f;
      }
      acc = mad->output("Value");
    }

    if (acc && constPart != 0.f) {
      auto *add = graph->create_node<ccl::MathNode>();
      add->set_math_type(ccl::NODE_MATH_ADD);
      graph->connect(acc, add->input("Value1"));
      add->input("Value2")->set(constPart);
      acc = add->output("Value");
    }

    if (!acc) { // fully constant alpha
      auto *value = graph->create_node<ccl::ValueNode>();
      value->set_value(constPart);
      acc = value->output("Value");
    }

    out.alpha = acc;
  }

  return out;
}

Sampler::SamplerOutputs Sampler::makeStandardOutputs(
    ccl::ShaderGraph *graph, const ColorAlpha &value)
{
  SamplerOutputs outputs;
  outputs.colorOutput = value.color;
  outputs.alphaOutput = value.alpha;

  // Scalar output: first component of the color
  auto *separate = graph->create_node<ccl::SeparateXYZNode>();
  graph->connect(value.color, separate->input("Vector"));
  outputs.scalarOutput = separate->output("X");

  // Normal-map output: flip Y (DirectX-style normal maps)
  auto *flip = graph->create_node<ccl::VectorMathNode>();
  flip->set_math_type(ccl::NODE_VECTOR_MATH_MULTIPLY);
  flip->set_vector1(make_float3(1.f, -1.f, 1.f));
  graph->connect(value.color, flip->input("Vector2"));
  outputs.normalOutput = flip->output("Vector");

  return outputs;
}

// Sampler definitions ////////////////////////////////////////////////////////

Sampler::Sampler(CyclesGlobalState *s) : Object(ANARI_SAMPLER, s) {}

Sampler::~Sampler() = default;

void Sampler::commitParameters()
{
  m_inAttribute = getParamString("inAttribute", "attribute0");
  m_inTransform = getParam<mat4>("inTransform", mat4(linalg::identity));
  m_inOffset =
      getParam<helium::float4>("inOffset", helium::float4(0.f, 0.f, 0.f, 0.f));
  m_outTransform = getParam<mat4>("outTransform", mat4(linalg::identity));
  m_outOffset =
      getParam<helium::float4>("outOffset", helium::float4(0.f, 0.f, 0.f, 0.f));
}

Sampler::SamplerOutputs Sampler::createNodeGraph(ccl::ShaderGraph *)
{
  return {};
}

Sampler *Sampler::createInstance(std::string_view subtype, CyclesGlobalState *s)
{
  if (subtype == "image1D")
    return new Image1D(s);
  else if (subtype == "image2D")
    return new Image2D(s);
  else if (subtype == "transform")
    return new TransformSampler(s);
  else if (subtype == "primitive")
    return new PrimitiveSampler(s);

  // Cycles no longer supports dense 3D image textures, so image3D is
  // intentionally represented by the same safe invalid object as any other
  // unknown subtype (with a clearer warning, see UnknownSampler).
  return new UnknownSampler(subtype, s);
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Sampler *);
