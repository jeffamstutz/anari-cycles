// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Material.h"

namespace anari_cycles {

// HairMaterial ///////////////////////////////////////////////////////////////
//
// CYCLES_MATERIAL_HAIR: 'hair' material subtype wrapping Cycles' Principled
// Hair BSDF, intended for 'curve' geometry. 'colorMode' selects how the fiber
// absorption is parametrized: "color" (direct reflectance from 'color'),
// "melanin" ('melanin'/'melaninRedness'/'tint') or "absorption"
// ('absorptionCoefficient'). 'model' picks the scattering model ("huang" for
// far-field elliptical fibers, "chiang" for near-field circular ones). All
// defaults equal the Cycles socket defaults.

struct HairMaterial : public Material
{
  HairMaterial(CyclesGlobalState *s);
  ~HairMaterial() override = default;

  void commitParameters() override;
  void finalize() override;

 private:
  void makeGraph() override;

  ccl::PrincipledHairBsdfNode *m_bsdf{nullptr};

  std::string m_colorMode{"color"};
  std::string m_model{"huang"};

  std::string m_colorAttr;
  float3 m_color{make_float3(0.017513f, 0.005763f, 0.002059f)};
  helium::ChangeObserverPtr<Sampler> m_colorSampler;

  float m_melanin{0.8f};
  float m_melaninRedness{1.f};
  float3 m_tint{make_float3(1.f, 1.f, 1.f)};
  float3 m_absorptionCoefficient{make_float3(0.245531f, 0.52f, 1.365f)};

  float m_roughness{0.3f};
  float m_radialRoughness{0.3f};
  float m_coat{0.f};
  float m_ior{1.55f};
  float m_offset{2.f * M_PI_F / 180.f};
  float m_randomColor{0.f};
  float m_randomRoughness{0.f};
  float m_aspectRatio{0.85f};
};

} // namespace anari_cycles
