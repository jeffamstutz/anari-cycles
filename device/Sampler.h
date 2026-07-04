// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "SamplerImageLoader.h"
// std
#include <memory>
// cycles
#include "scene/image.h"
#include "scene/shader_graph.h"

namespace anari_cycles {

struct Sampler : public Object
{
  Sampler(CyclesGlobalState *s);
  ~Sampler() override;

  static Sampler *createInstance(
      std::string_view subtype, CyclesGlobalState *s);

  void commitParameters() override;

  // Shader-graph outputs a sampler contributes to a material graph.
  struct SamplerOutputs
  {
    ccl::ShaderOutput *colorOutput{nullptr};
    ccl::ShaderOutput *scalarOutput{nullptr};
    ccl::ShaderOutput *normalOutput{nullptr};
    // 4th (alpha) component of the sampled value; nullptr means constant 1
    ccl::ShaderOutput *alphaOutput{nullptr};
  };

  // Create and configure the sampler's node graph inside 'graph', returning
  // its outputs. The base implementation returns empty outputs (used by
  // unknown/invalid samplers).
  virtual SamplerOutputs createNodeGraph(ccl::ShaderGraph *graph);

 protected:
  // A color signal with an optional alpha channel; alpha == nullptr means the
  // alpha is the constant 1.
  struct ColorAlpha
  {
    ccl::ShaderOutput *color{nullptr};
    ccl::ShaderOutput *alpha{nullptr};
  };

  // Fetch the ANARI attribute 'inAttribute' names ('color', 'attribute0'..3,
  // 'primitiveId') as graph outputs; unsupported names warn and yield the
  // constant (0,0,0,1).
  ColorAlpha makeAttributeInput(
      ccl::ShaderGraph *graph, const std::string &attribute) const;

  // result = m * (in.rgb, in.a) + offset, as graph nodes (full affine,
  // including rotation/shear, the matrix' 4th column and the alpha row).
  static ColorAlpha applyAffineTransform(ccl::ShaderGraph *graph,
      const ColorAlpha &in,
      const mat4 &m,
      const helium::float4 &offset);

  // Standard output set derived from a color/alpha pair (scalar = first
  // component, normal = color with Y flipped for normal maps).
  static SamplerOutputs makeStandardOutputs(
      ccl::ShaderGraph *graph, const ColorAlpha &value);

  ccl::ImageHandle m_handle{};
  std::string m_inAttribute{"attribute0"};
  mat4 m_inTransform{mat4(linalg::identity)};
  helium::float4 m_inOffset{0.f, 0.f, 0.f, 0.f};
  mat4 m_outTransform{mat4(linalg::identity)};
  helium::float4 m_outOffset{0.f, 0.f, 0.f, 0.f};
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Sampler *, ANARI_SAMPLER);
