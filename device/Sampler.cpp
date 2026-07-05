// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Sampler.h"
#include "Array.h"
#include "cycles_math.h"
#include "util/colorspace.h"

namespace anari_cycles {

// Helper functions ///////////////////////////////////////////////////////////

// Canonical Cycles attribute name for an ANARI attribute string (the names
// the geometries upload their attribute channels under, see Geometry.cpp).
static const char *cyclesAttributeName(const std::string &attribute)
{
  if (attribute == "color")
    return "vertex.color";
  if (attribute == "attribute0")
    return "vertex.attribute0";
  if (attribute == "attribute1")
    return "vertex.attribute1";
  if (attribute == "attribute2")
    return "vertex.attribute2";
  if (attribute == "attribute3")
    return "vertex.attribute3";
  return nullptr;
}

static ccl::ustring imageColorspace(anari::DataType type)
{
  switch (type) {
  case ANARI_UFIXED8_R_SRGB:
  case ANARI_UFIXED8_RA_SRGB:
  case ANARI_UFIXED8_RGB_SRGB:
  case ANARI_UFIXED8_RGBA_SRGB:
    return ccl::u_colorspace_srgb;
  default:
    return ccl::u_colorspace_data;
  }
}

// Map a single ANARI wrap mode onto the Cycles extension that implements it.
static ExtensionType cyclesExtension(helium::WrapMode mode)
{
  switch (mode) {
  case helium::WrapMode::REPEAT:
    return EXTENSION_REPEAT;
  case helium::WrapMode::MIRROR_REPEAT:
    return EXTENSION_MIRROR;
  case helium::WrapMode::CLAMP_TO_EDGE:
  default:
    return EXTENSION_EXTEND;
  }
}

// Wrap one texture-coordinate axis in the shader graph so the image can be
// sampled with EXTENSION_REPEAT even when the two axes use different ANARI
// wrap modes (Cycles has a single extension for all axes). Coordinates are
// pre-wrapped into [halfTexel, 1 - halfTexel] (clamp/mirror) or passed
// through (repeat, handled natively by the base extension); keeping the
// filter taps inside the image makes the emulation exact.
static ccl::ShaderOutput *wrapAxis(ccl::ShaderGraph *graph,
    ccl::ShaderOutput *value,
    helium::WrapMode mode,
    size_t size)
{
  if (mode == helium::WrapMode::REPEAT)
    return value; // native via EXTENSION_REPEAT

  const float halfTexel = 0.5f / float(std::max<size_t>(size, 1));

  if (mode == helium::WrapMode::MIRROR_REPEAT) {
    auto *pingpong = graph->create_node<ccl::MathNode>();
    pingpong->set_math_type(ccl::NODE_MATH_PINGPONG);
    graph->connect(value, pingpong->input("Value1"));
    pingpong->input("Value2")->set(1.f);
    value = pingpong->output("Value");
  }

  auto *lo = graph->create_node<ccl::MathNode>();
  lo->set_math_type(ccl::NODE_MATH_MAXIMUM);
  graph->connect(value, lo->input("Value1"));
  lo->input("Value2")->set(halfTexel);

  auto *hi = graph->create_node<ccl::MathNode>();
  hi->set_math_type(ccl::NODE_MATH_MINIMUM);
  graph->connect(lo->output("Value"), hi->input("Value1"));
  hi->input("Value2")->set(1.f - halfTexel);

  return hi->output("Value");
}

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

// Image2D Sampler ////////////////////////////////////////////////////////////

struct Image2D : public Sampler
{
  Image2D(CyclesGlobalState *d);

  bool isValid() const override;
  void commitParameters() override;
  void finalize() override;

  SamplerOutputs createNodeGraph(ccl::ShaderGraph *graph) override;

 private:
  // Cycles has a single extension mode for all image axes (stored in the
  // ImageParams at add_image() time -- the node socket is ignored for
  // pre-made handles). Differing ANARI per-axis wrap modes are emulated by
  // pre-wrapping the coordinates in the graph and sampling with
  // EXTENSION_REPEAT (see wrapAxis()).
  ExtensionType baseExtension() const
  {
    return m_wrapMode1 == m_wrapMode2 ? cyclesExtension(m_wrapMode1)
                                      : EXTENSION_REPEAT;
  }

  helium::IntrusivePtr<Array2D> m_image;
  helium::WrapMode m_wrapMode1{helium::WrapMode::DEFAULT};
  helium::WrapMode m_wrapMode2{helium::WrapMode::DEFAULT};
  bool m_linearFilter{true};
};

Image2D::Image2D(CyclesGlobalState *d) : Sampler(d) {}

bool Image2D::isValid() const
{
  return m_image;
}

void Image2D::commitParameters()
{
  Sampler::commitParameters();
  m_image = getParamObject<Array2D>("image");
  m_linearFilter = getParamString("filter", "linear") != "nearest";
  m_wrapMode1 =
      helium::wrapModeFromString(getParamString("wrapMode1", "clampToEdge"));
  m_wrapMode2 =
      helium::wrapModeFromString(getParamString("wrapMode2", "clampToEdge"));
}

void Image2D::finalize()
{
  if (isValid()) {
    auto &state = *deviceState();
    auto loader = std::make_unique<SamplerImageLoader>(m_image.ptr);
    ccl::ImageParams params;
    params.alpha_type = IMAGE_ALPHA_AUTO;
    params.colorspace = imageColorspace(m_image->elementType());
    params.extension = baseExtension();
    params.interpolation =
        m_linearFilter ? INTERPOLATION_LINEAR : INTERPOLATION_CLOSEST;
    m_handle =
        state.scene->image_manager->add_image(std::move(loader), params, false);
  }
  // notify observing materials so they rebuild their graphs on the new handle
  Object::finalize();
}

Sampler::SamplerOutputs Image2D::createNodeGraph(ccl::ShaderGraph *graph)
{
  if (!graph || m_handle.empty())
    return {};

  auto in = makeAttributeInput(graph, m_inAttribute);
  auto uv = applyAffineTransform(graph, in, m_inTransform, m_inOffset);

  ccl::ShaderOutput *coords = uv.color;
  if (m_wrapMode1 != m_wrapMode2) {
    auto *separate = graph->create_node<ccl::SeparateXYZNode>();
    graph->connect(coords, separate->input("Vector"));
    auto *combine = graph->create_node<ccl::CombineXYZNode>();
    graph->connect(wrapAxis(graph, separate->output("X"), m_wrapMode1,
                       m_image->size(0)),
        combine->input("X"));
    graph->connect(wrapAxis(graph, separate->output("Y"), m_wrapMode2,
                       m_image->size(1)),
        combine->input("Y"));
    coords = combine->output("Vector");
  }

  auto *tex = graph->create_node<ccl::ImageTextureNode>();
  tex->handle = m_handle;
  tex->set_colorspace(ccl::u_colorspace_auto);
  tex->set_extension(baseExtension());
  tex->set_interpolation(
      m_linearFilter ? INTERPOLATION_LINEAR : INTERPOLATION_CLOSEST);
  graph->connect(coords, tex->input("Vector"));

  ColorAlpha sampled{tex->output("Color"), tex->output("Alpha")};
  auto out = applyAffineTransform(graph, sampled, m_outTransform, m_outOffset);
  return makeStandardOutputs(graph, out);
}

// Image1D Sampler ////////////////////////////////////////////////////////////

struct Image1D : public Sampler
{
  Image1D(CyclesGlobalState *d);

  bool isValid() const override;
  void commitParameters() override;
  void finalize() override;

  SamplerOutputs createNodeGraph(ccl::ShaderGraph *graph) override;

 private:
  // Observed so committing a change on the array (new data or a new
  // 'region' -- KHR_ARRAY1D_REGION) re-finalizes this sampler.
  helium::ChangeObserverPtr<Array1D> m_image;
  helium::WrapMode m_wrapMode{helium::WrapMode::DEFAULT};
  bool m_linearFilter{true};
};

Image1D::Image1D(CyclesGlobalState *d) : Sampler(d), m_image(this) {}

bool Image1D::isValid() const
{
  return m_image;
}

void Image1D::commitParameters()
{
  Sampler::commitParameters();
  m_image = getParamObject<Array1D>("image");
  m_linearFilter = getParamString("filter", "linear") != "nearest";
  // the registry names this 'wrapMode'; accept the common 'wrapMode1' too
  m_wrapMode = helium::wrapModeFromString(
      getParamString("wrapMode", getParamString("wrapMode1", "clampToEdge")));
}

void Image1D::finalize()
{
  if (isValid()) {
    auto &state = *deviceState();
    auto loader = std::make_unique<SamplerImageLoader>(m_image.get());
    ccl::ImageParams params;
    params.alpha_type = IMAGE_ALPHA_AUTO;
    params.colorspace = imageColorspace(m_image->elementType());
    params.extension = cyclesExtension(m_wrapMode);
    params.interpolation =
        m_linearFilter ? INTERPOLATION_LINEAR : INTERPOLATION_CLOSEST;
    m_handle =
        state.scene->image_manager->add_image(std::move(loader), params, false);
  }
  // notify observing materials so they rebuild their graphs on the new handle
  Object::finalize();
}

Sampler::SamplerOutputs Image1D::createNodeGraph(ccl::ShaderGraph *graph)
{
  if (!graph || m_handle.empty())
    return {};

  auto in = makeAttributeInput(graph, m_inAttribute);
  auto uv = applyAffineTransform(graph, in, m_inTransform, m_inOffset);

  auto *tex = graph->create_node<ccl::ImageTextureNode>();
  tex->handle = m_handle;
  tex->set_colorspace(ccl::u_colorspace_auto);
  tex->set_extension(cyclesExtension(m_wrapMode));
  tex->set_interpolation(
      m_linearFilter ? INTERPOLATION_LINEAR : INTERPOLATION_CLOSEST);
  graph->connect(uv.color, tex->input("Vector"));

  ColorAlpha sampled{tex->output("Color"), tex->output("Alpha")};
  auto out = applyAffineTransform(graph, sampled, m_outTransform, m_outOffset);
  return makeStandardOutputs(graph, out);
}

// Transform Sampler //////////////////////////////////////////////////////////

struct TransformSampler : public Sampler
{
  TransformSampler(CyclesGlobalState *d) : Sampler(d) {}

  bool isValid() const override
  {
    return true;
  }

  void commitParameters() override
  {
    Sampler::commitParameters();
    // 'outTransform' is the registry name; 'transform' is the legacy alias
    // helide reads and the CTS sets.
    if (!hasParam("outTransform"))
      m_outTransform = getParam<mat4>("transform", mat4(linalg::identity));
  }

  SamplerOutputs createNodeGraph(ccl::ShaderGraph *graph) override
  {
    if (!graph)
      return {};
    auto in = makeAttributeInput(graph, m_inAttribute);
    auto out = applyAffineTransform(graph, in, m_outTransform, m_outOffset);
    return makeStandardOutputs(graph, out);
  }
};

// Primitive Sampler //////////////////////////////////////////////////////////

// Implemented as a nearest-filtered 1D texture lookup indexed by the
// per-primitive 'primitiveId' float attribute that every geometry uploads
// (see writePrimitiveId() in Geometry.cpp): u = (id + inOffset + 0.5) / N.
// Because the index travels through a float32 attribute and shader math,
// lookups are exact only for primitiveId + inOffset < 2^24.
struct PrimitiveSampler : public Sampler
{
  PrimitiveSampler(CyclesGlobalState *d) : Sampler(d), m_array(this) {}

  bool isValid() const override
  {
    return m_array;
  }

  void commitParameters() override
  {
    Sampler::commitParameters();
    m_array = getParamObject<Array1D>("array");
    m_offset = getParam<uint64_t>(
        "inOffset", uint64_t(getParam<uint32_t>("inOffset", 0)));
  }

  void finalize() override
  {
    if (isValid()) {
      auto &state = *deviceState();
      auto loader = std::make_unique<SamplerImageLoader>(m_array.get());
      ccl::ImageParams params;
      params.alpha_type = IMAGE_ALPHA_AUTO;
      params.colorspace = imageColorspace(m_array->elementType());
      params.extension = EXTENSION_EXTEND; // clamp out-of-range indices
      params.interpolation = INTERPOLATION_CLOSEST;
      m_handle = state.scene->image_manager->add_image(
          std::move(loader), params, false);
    }
    // notify observing materials so they rebuild their graphs on the new
    // handle
    Object::finalize();
  }

  SamplerOutputs createNodeGraph(ccl::ShaderGraph *graph) override
  {
    if (!graph || m_handle.empty())
      return {};

    auto *id = graph->create_node<ccl::AttributeNode>();
    id->set_attribute(ccl::ustring("primitiveId"));

    const float n = float(m_array->totalSize());
    auto *mad = graph->create_node<ccl::MathNode>();
    mad->set_math_type(ccl::NODE_MATH_MULTIPLY_ADD);
    graph->connect(id->output("Fac"), mad->input("Value1"));
    mad->input("Value2")->set(1.f / n);
    mad->input("Value3")->set((float(m_offset) + 0.5f) / n);

    auto *coords = graph->create_node<ccl::CombineXYZNode>();
    graph->connect(mad->output("Value"), coords->input("X"));
    coords->set_y(0.5f);

    auto *tex = graph->create_node<ccl::ImageTextureNode>();
    tex->handle = m_handle;
    tex->set_colorspace(ccl::u_colorspace_auto);
    tex->set_extension(EXTENSION_EXTEND); // clamp out-of-range indices
    tex->set_interpolation(INTERPOLATION_CLOSEST);
    graph->connect(coords->output("Vector"), tex->input("Vector"));

    return makeStandardOutputs(
        graph, {tex->output("Color"), tex->output("Alpha")});
  }

 private:
  // Observed so committing a change on the array (new data or a new
  // 'region' -- KHR_ARRAY1D_REGION) re-finalizes this sampler.
  helium::ChangeObserverPtr<Array1D> m_array;
  uint64_t m_offset{0};
};

// Unknown Sampler ////////////////////////////////////////////////////////////

struct UnknownSampler : public Sampler
{
  UnknownSampler(std::string_view subtype, CyclesGlobalState *s)
      : Sampler(s), m_subtype(subtype)
  {
    if (m_subtype == "image3D") {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'image3D' samplers are not supported by the cycles device (Cycles "
          "has no dense 3D image textures); the sampler is treated as an "
          "unknown object");
    } else {
      reportMessage(ANARI_SEVERITY_WARNING,
          "created unknown ANARI_SAMPLER object of subtype '%s'",
          m_subtype.c_str());
    }
  }

  bool isValid() const override
  {
    return false;
  }

  void warnIfUnknownObject() const override
  {
    reportMessage(ANARI_SEVERITY_WARNING,
        "encountered unknown ANARI_SAMPLER object of subtype '%s'",
        m_subtype.c_str());
  }

 private:
  std::string m_subtype;
};

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
