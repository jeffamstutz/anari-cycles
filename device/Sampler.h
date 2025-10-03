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

  virtual ccl::ImageHandle getCyclesImageHandle();
  mat4 getInTransform() const { return m_inTransform; }
  helium::float4 getInOffset() const { return m_inOffset; }
  virtual mat4 getOutTransform() const { return mat4(linalg::identity); }
  virtual helium::float4 getOutOffset() const { return helium::float4(0.f, 0.f, 0.f, 0.f); }
  
  // Apply input coordinate transformation to UV coordinates
  virtual ccl::ShaderOutput *applyInputTransform(ccl::ShaderGraph *graph, 
                                                  ccl::ShaderOutput *uvInput);

  // New interface for sampler-designed node graphs
  struct SamplerOutputs {
    ccl::ShaderOutput *colorOutput{nullptr};
    ccl::ShaderOutput *scalarOutput{nullptr};
    ccl::ShaderOutput *normalOutput{nullptr};
  };
  
  // Create and configure the sampler's node graph, returning outputs
  virtual SamplerOutputs createNodeGraph(ccl::ShaderGraph *graph, 
                                         ccl::ShaderOutput *uvInput);
  
  // Check if this sampler provides a color output
  virtual bool hasColorOutput() const { return true; }
  
  // Check if this sampler provides a scalar output  
  virtual bool hasScalarOutput() const { return true; }

 protected:
  ccl::ImageHandle m_handle{};
  mat4 m_inTransform{mat4(linalg::identity)};
  helium::float4 m_inOffset{0.f, 0.f, 0.f, 0.f};
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Sampler *, ANARI_SAMPLER);
