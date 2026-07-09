// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Sampler.h"
#include "cycles_math.h"

namespace anari_cycles {

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

} // namespace anari_cycles
