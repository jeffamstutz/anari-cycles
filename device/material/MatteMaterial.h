// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Material.h"

namespace anari_cycles {

struct MatteMaterial : public Material
{
  MatteMaterial(CyclesGlobalState *s);
  ~MatteMaterial() override = default;

  void commitParameters() override;
  void finalize() override;

 private:
  void makeGraph() override;

  ccl::PrincipledBsdfNode *m_bsdf{nullptr};

  std::string m_colorAttr;
  float3 m_color{make_float3(0.8f, 0.8f, 0.8f)};
  helium::ChangeObserverPtr<Sampler> m_colorSampler;

  std::string m_opacityAttr;
  float m_opacity{1.f};
  helium::ChangeObserverPtr<Sampler> m_opacitySampler;

  helium::AlphaMode m_mode{helium::AlphaMode::OPAQUE};
  float m_alphaCutoff{0.5f};
};

} // namespace anari_cycles
