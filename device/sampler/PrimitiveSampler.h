// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "SamplerUtils.h"
#include "array/Array1D.h"

namespace anari_cycles {

// Implemented as a nearest-filtered 1D texture lookup indexed by the
// per-primitive 'primitiveId' float attribute that every geometry uploads
// (see writePrimitiveId() in geometry/GeometryAttributes.h): u = (id + inOffset + 0.5) / N.
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

} // namespace anari_cycles
