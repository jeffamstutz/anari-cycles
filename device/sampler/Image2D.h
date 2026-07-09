// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "SamplerUtils.h"
#include "array/Array2D.h"

namespace anari_cycles {

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

} // namespace anari_cycles
