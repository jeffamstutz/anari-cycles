// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Sampler.h"
#include "array/Array1D.h"

namespace anari_cycles {

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

} // namespace anari_cycles
