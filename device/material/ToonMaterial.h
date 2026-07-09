// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Material.h"

namespace anari_cycles {

// ToonMaterial ///////////////////////////////////////////////////////////////
//
// CYCLES_MATERIAL_TOON: 'toon' material subtype wrapping Cycles' Toon BSDF
// (cel-shading lobe with a hard light/dark transition). 'component' selects
// the diffuse or glossy variant; 'size' [0,1] sets the angular extent of the
// lit region and 'smooth' the softness of its boundary. The BSDF has no alpha
// input, so opacity/alphaMode are not offered.

struct ToonMaterial : public Material
{
  ToonMaterial(CyclesGlobalState *s);
  ~ToonMaterial() override = default;

  void commitParameters() override;
  void finalize() override;

 private:
  void makeGraph() override;

  ccl::ToonBsdfNode *m_bsdf{nullptr};

  std::string m_colorAttr;
  float3 m_color{make_float3(0.8f, 0.8f, 0.8f)};
  helium::ChangeObserverPtr<Sampler> m_colorSampler;

  std::string m_component{"diffuse"};

  std::string m_sizeAttr;
  float m_size{0.5f};
  helium::ChangeObserverPtr<Sampler> m_sizeSampler;

  std::string m_smoothAttr;
  float m_smooth{0.f};
  helium::ChangeObserverPtr<Sampler> m_smoothSampler;
};

} // namespace anari_cycles
