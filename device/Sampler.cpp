// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Sampler.h"
#include "Array.h"
#include "cycles_math.h"
#include "scene/colorspace.h"

namespace anari_cycles {

// Image2D Sampler ////////////////////////////////////////////////////////////

struct Image2D : public Sampler
{
  Image2D(CyclesGlobalState *d);

  bool isValid() const override;
  void commitParameters() override;
  void finalize() override;

  mat4 getOutTransform() const override { return m_outTransform; }
  helium::float4 getOutOffset() const override { return m_outOffset; }

  // Override to provide custom node graph if needed
  SamplerOutputs createNodeGraph(ccl::ShaderGraph *graph, 
                                 ccl::ShaderOutput *uvInput) override;

 private:
  helium::IntrusivePtr<Array2D> m_image;
  helium::Attribute m_inAttribute{helium::Attribute::NONE};
  helium::WrapMode m_wrapMode1{helium::WrapMode::DEFAULT};
  helium::WrapMode m_wrapMode2{helium::WrapMode::DEFAULT};
  bool m_linearFilter{true};
  mat4 m_outTransform{mat4(linalg::identity)};
  helium::float4 m_outOffset{0.f, 0.f, 0.f, 0.f};
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
  m_inAttribute =
      helium::attributeFromString(getParamString("inAttribute", "attribute0"));
  m_linearFilter = getParamString("filter", "linear") != "nearest";
  m_wrapMode1 =
      helium::wrapModeFromString(getParamString("wrapMode1", "clampToEdge"));
  m_wrapMode2 =
      helium::wrapModeFromString(getParamString("wrapMode2", "clampToEdge"));
  m_outTransform = getParam<mat4>("outTransform", mat4(linalg::identity));
  m_outOffset =
      getParam<helium::float4>("outOffset", helium::float4(0.f, 0.f, 0.f, 0.f));
}

void Image2D::finalize()
{
  if (!isValid())
    return;

  auto &state = *deviceState();
  auto loader = std::make_unique<SamplerImageLoader>(m_image.ptr);
  ccl::ImageParams params;
  params.alpha_type = IMAGE_ALPHA_AUTO;
  if (m_image->elementType() == ANARI_UFIXED8_RGBA_SRGB
      || m_image->elementType() == ANARI_UFIXED8_RGB_SRGB) {
    params.colorspace = ccl::u_colorspace_srgb;
  } else {
    params.colorspace = ccl::u_colorspace_raw;
  }
  params.interpolation =
      m_linearFilter ? INTERPOLATION_LINEAR : INTERPOLATION_CLOSEST;
  m_handle =
      state.scene->image_manager->add_image(std::move(loader), params, false);
}

Sampler::SamplerOutputs Image2D::createNodeGraph(ccl::ShaderGraph *graph, 
                                                  ccl::ShaderOutput *uvInput) 
{
  // For now, use the default implementation
  // In the future, this could be customized for specific image types
  // or filtering modes
  return Sampler::createNodeGraph(graph, uvInput);
}

// Sampler definitions ////////////////////////////////////////////////////////

Sampler::Sampler(CyclesGlobalState *s) : Object(ANARI_SAMPLER, s) {}

Sampler::~Sampler() = default;

void Sampler::commitParameters()
{
  m_inTransform = getParam<mat4>("inTransform", mat4(linalg::identity));
  m_inOffset =
      getParam<helium::float4>("inOffset", helium::float4(0.f, 0.f, 0.f, 0.f));
}

ccl::ShaderOutput *Sampler::applyInputTransform(ccl::ShaderGraph *graph, 
                                                 ccl::ShaderOutput *uvInput)
{
  if (!graph || !uvInput)
    return uvInput;

  // Check if we have non-identity input transforms
  mat4 identity = mat4(linalg::identity);
  bool hasInTransform = linalg::any(linalg::nequal(m_inTransform, identity));
  bool hasInOffset = linalg::length(m_inOffset.xyz()) > 1e-6f;
  
  if (!hasInTransform && !hasInOffset) {
    // No transformation needed, return original UV input
    return uvInput;
  }

  // Create mapping node for input transformation
  auto *inMapping = graph->create_node<ccl::MappingNode>();
  
  // Convert mat4 to scale, rotation, location for MappingNode
  float3 scale = make_float3(
      linalg::length(m_inTransform.x.xyz()),
      linalg::length(m_inTransform.y.xyz()),
      linalg::length(m_inTransform.z.xyz()));
  
  // For simplicity, we'll treat the matrix as a scale + translation for now
  float3 location = make_float3(m_inOffset.x, m_inOffset.y, m_inOffset.z);
  inMapping->set_scale(scale);
  inMapping->set_location(location);
  inMapping->set_rotation(make_float3(0.f, 0.f, 0.f));

  // Connect UV coordinates through input mapping
  graph->connect(uvInput, inMapping->input("Vector"));
  
  return inMapping->output("Vector");
}

Sampler::SamplerOutputs Sampler::createNodeGraph(ccl::ShaderGraph *graph, 
                                                  ccl::ShaderOutput *uvInput)
{
  SamplerOutputs outputs;
  
  if (!graph || !uvInput || m_handle.empty())
    return outputs;

  // Apply input transformation
  ccl::ShaderOutput *transformedUV = applyInputTransform(graph, uvInput);
  
  // Create the basic image texture node
  auto *textureNode = graph->create_node<ccl::ImageTextureNode>();
  textureNode->handle = m_handle;
  textureNode->set_colorspace(ccl::u_colorspace_auto);
  graph->connect(transformedUV, textureNode->input("Vector"));
  
  // Handle output transforms
  mat4 outTransformMat = getOutTransform();
  helium::float4 outOffsetVec = getOutOffset();
  
  mat4 identity = mat4(linalg::identity);
  bool hasOutTransform = linalg::any(linalg::nequal(outTransformMat, identity));  
  bool hasOutOffset = linalg::length(outOffsetVec.xyz()) > 1e-6f;

  ccl::ShaderOutput *colorOutput = textureNode->output("Color");
  ccl::ShaderOutput *scalarOutput = nullptr;
  
  if (hasOutTransform || hasOutOffset) {
    // Handle full matrix transformation including swizzling/remapping
    // This allows for arbitrary linear transformations of the color channels,
    // including swizzling operations like remapping green to red channel
    if (hasOutTransform) {
      // Create separate RGB nodes to extract individual components
      auto *separateRGB = graph->create_node<ccl::SeparateRGBNode>();
      graph->connect(colorOutput, separateRGB->input("Image"));
      
      // Extract the 3x3 portion of the transform matrix for color transformation
      // outTransformMat is column-major: [x_col, y_col, z_col, w_col]
      // Matrix coefficients for transformation: result = transform * input
      float m00 = outTransformMat.x.x, m01 = outTransformMat.y.x, m02 = outTransformMat.z.x;  // Row 0
      float m10 = outTransformMat.x.y, m11 = outTransformMat.y.y, m12 = outTransformMat.z.y;  // Row 1
      float m20 = outTransformMat.x.z, m21 = outTransformMat.y.z, m22 = outTransformMat.z.z;  // Row 2
      
      // Create math nodes for matrix multiplication: result = transform * input
      // Each output component is a dot product of a matrix row with the input RGB
      auto *redMath1 = graph->create_node<ccl::MathNode>();
      auto *redMath2 = graph->create_node<ccl::MathNode>();
      auto *redMath3 = graph->create_node<ccl::MathNode>();
      auto *redAdd1 = graph->create_node<ccl::MathNode>();
      auto *redAdd2 = graph->create_node<ccl::MathNode>();
      
      auto *greenMath1 = graph->create_node<ccl::MathNode>();
      auto *greenMath2 = graph->create_node<ccl::MathNode>();
      auto *greenMath3 = graph->create_node<ccl::MathNode>();
      auto *greenAdd1 = graph->create_node<ccl::MathNode>();
      auto *greenAdd2 = graph->create_node<ccl::MathNode>();
      
      auto *blueMath1 = graph->create_node<ccl::MathNode>();
      auto *blueMath2 = graph->create_node<ccl::MathNode>();
      auto *blueMath3 = graph->create_node<ccl::MathNode>();
      auto *blueAdd1 = graph->create_node<ccl::MathNode>();
      auto *blueAdd2 = graph->create_node<ccl::MathNode>();
      
      // Set up multiply and add operations
      redMath1->set_math_type(ccl::NODE_MATH_MULTIPLY);
      redMath2->set_math_type(ccl::NODE_MATH_MULTIPLY);
      redMath3->set_math_type(ccl::NODE_MATH_MULTIPLY);
      redAdd1->set_math_type(ccl::NODE_MATH_ADD);
      redAdd2->set_math_type(ccl::NODE_MATH_ADD);
      
      greenMath1->set_math_type(ccl::NODE_MATH_MULTIPLY);
      greenMath2->set_math_type(ccl::NODE_MATH_MULTIPLY);
      greenMath3->set_math_type(ccl::NODE_MATH_MULTIPLY);
      greenAdd1->set_math_type(ccl::NODE_MATH_ADD);
      greenAdd2->set_math_type(ccl::NODE_MATH_ADD);
      
      blueMath1->set_math_type(ccl::NODE_MATH_MULTIPLY);
      blueMath2->set_math_type(ccl::NODE_MATH_MULTIPLY);
      blueMath3->set_math_type(ccl::NODE_MATH_MULTIPLY);
      blueAdd1->set_math_type(ccl::NODE_MATH_ADD);
      blueAdd2->set_math_type(ccl::NODE_MATH_ADD);
      
      // Set matrix values as constants
      redMath1->set_value2(m00);
      redMath2->set_value2(m01);
      redMath3->set_value2(m02);
      
      greenMath1->set_value2(m10);
      greenMath2->set_value2(m11);
      greenMath3->set_value2(m12);
      
      blueMath1->set_value2(m20);
      blueMath2->set_value2(m21);
      blueMath3->set_value2(m22);
      
      // Connect RGB components to multiply nodes
      graph->connect(separateRGB->output("R"), redMath1->input("Value1"));
      graph->connect(separateRGB->output("G"), redMath2->input("Value1"));
      graph->connect(separateRGB->output("B"), redMath3->input("Value1"));
      
      graph->connect(separateRGB->output("R"), greenMath1->input("Value1"));
      graph->connect(separateRGB->output("G"), greenMath2->input("Value1"));
      graph->connect(separateRGB->output("B"), greenMath3->input("Value1"));
      
      graph->connect(separateRGB->output("R"), blueMath1->input("Value1"));
      graph->connect(separateRGB->output("G"), blueMath2->input("Value1"));
      graph->connect(separateRGB->output("B"), blueMath3->input("Value1"));
      
      // Chain additions for each color component
      graph->connect(redMath1->output("Value"), redAdd1->input("Value1"));
      graph->connect(redMath2->output("Value"), redAdd1->input("Value2"));
      graph->connect(redAdd1->output("Value"), redAdd2->input("Value1"));
      graph->connect(redMath3->output("Value"), redAdd2->input("Value2"));
      
      graph->connect(greenMath1->output("Value"), greenAdd1->input("Value1"));
      graph->connect(greenMath2->output("Value"), greenAdd1->input("Value2"));
      graph->connect(greenAdd1->output("Value"), greenAdd2->input("Value1"));
      graph->connect(greenMath3->output("Value"), greenAdd2->input("Value2"));
      
      graph->connect(blueMath1->output("Value"), blueAdd1->input("Value1"));
      graph->connect(blueMath2->output("Value"), blueAdd1->input("Value2"));
      graph->connect(blueAdd1->output("Value"), blueAdd2->input("Value1"));
      graph->connect(blueMath3->output("Value"), blueAdd2->input("Value2"));
      
      // Combine back into RGB
      auto *combineRGB = graph->create_node<ccl::CombineRGBNode>();
      graph->connect(redAdd2->output("Value"), combineRGB->input("R"));
      graph->connect(greenAdd2->output("Value"), combineRGB->input("G"));
      graph->connect(blueAdd2->output("Value"), combineRGB->input("B"));
      
      colorOutput = combineRGB->output("Image");
    }
    


    // Apply offset if present
    if (hasOutOffset) {
      auto *outOffsetNode = graph->create_node<ccl::VectorMathNode>();
      outOffsetNode->set_math_type(ccl::NODE_VECTOR_MATH_ADD);
      float3 outOffsetVector = make_float3(outOffsetVec.x, outOffsetVec.y, outOffsetVec.z);
      outOffsetNode->set_vector2(outOffsetVector);
      graph->connect(colorOutput, outOffsetNode->input("Vector1"));
      colorOutput = outOffsetNode->output("Vector");
    }
  }

  auto *normalOutput = graph->create_node<ccl::VectorMathNode>();
  normalOutput->set_math_type(ccl::NODE_VECTOR_MATH_MULTIPLY);
  normalOutput->set_vector1(make_float3(1.0f, -1.0f, 1.0f)); // Flip Y for normal maps
  graph->connect(colorOutput, normalOutput->input("Vector2"));

  // Create scalar output using SeparateXYZNode (extracts X component)
  auto *separateNode = graph->create_node<ccl::SeparateXYZNode>();
  graph->connect(colorOutput, separateNode->input("Vector"));
  scalarOutput = separateNode->output("X");
  
  outputs.colorOutput = colorOutput;
  outputs.scalarOutput = scalarOutput;
  outputs.normalOutput = normalOutput->output("Vector");
  
  return outputs;
}

Sampler *Sampler::createInstance(std::string_view subtype, CyclesGlobalState *s)
{
  if (subtype == "image2D")
    return new Image2D(s);
  else
    return (Sampler *)new UnknownObject(ANARI_SAMPLER, subtype, s);
}

ccl::ImageHandle Sampler::getCyclesImageHandle()
{
  return m_handle;
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Sampler *);
