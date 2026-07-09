// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Material.h"

namespace anari_cycles {

struct PhysicallyBasedMaterial : public Material
{
  PhysicallyBasedMaterial(CyclesGlobalState *s);
  ~PhysicallyBasedMaterial() override = default;

  void commitParameters() override;
  void finalize() override;

 private:
  void makeGraph() override;

  ccl::PrincipledBsdfNode *m_bsdf{nullptr};
  std::string m_colorAttr;
  float3 m_color{make_float3(1.f, 1.f, 1.f)};
  helium::ChangeObserverPtr<Sampler> m_colorSampler;

  std::string m_opacityAttr;
  float m_opacity{1.f};
  helium::ChangeObserverPtr<Sampler> m_opacitySampler;

  std::string m_roughnessAttr;
  float m_roughness{1.f};
  helium::ChangeObserverPtr<Sampler> m_roughnessSampler;

  std::string m_metallicAttr;
  float m_metallic{0.f};
  helium::ChangeObserverPtr<Sampler> m_metallicSampler;

  helium::ChangeObserverPtr<Sampler> m_normalSampler;

  std::string m_clearcoatAttr;
  float m_clearcoat{0.f};
  helium::ChangeObserverPtr<Sampler> m_clearcoatSampler;
  std::string m_clearcoatRoughnessAttr;
  float m_clearcoatRoughness{0.f};
  helium::ChangeObserverPtr<Sampler> m_clearcoatRoughnessSampler;
  helium::ChangeObserverPtr<Sampler> m_clearcoatNormalSampler;
  std::string m_emissiveAttr;
  float3 m_emissive{make_float3(0.f)};
  helium::ChangeObserverPtr<Sampler> m_emissiveSampler;
  std::string m_transmissionAttr;
  float m_transmission{0.f};
  helium::ChangeObserverPtr<Sampler> m_transmissionSampler;
  std::string m_iorAttr;
  float m_ior{1.5f};
  helium::ChangeObserverPtr<Sampler> m_iorSampler;

  std::string m_specularAttr;
  float m_specular{1.f};
  helium::ChangeObserverPtr<Sampler> m_specularSampler;
  std::string m_specularColorAttr;
  float3 m_specularColor{make_float3(1.f, 1.f, 1.f)};
  helium::ChangeObserverPtr<Sampler> m_specularColorSampler;

  std::string m_sheenColorAttr;
  float3 m_sheenColor{make_float3(0.f)};
  helium::ChangeObserverPtr<Sampler> m_sheenColorSampler;
  std::string m_sheenRoughnessAttr;
  float m_sheenRoughness{0.f};
  helium::ChangeObserverPtr<Sampler> m_sheenRoughnessSampler;

  float m_iridescence{0.f};
  float m_iridescenceIor{1.3f};
  float m_iridescenceThickness{0.f};

  // CYCLES_MATERIAL_SUBSURFACE vendor parameters (spec PBR has no SSS)
  std::string m_subsurfaceAttr;
  float m_subsurface{0.f};
  helium::ChangeObserverPtr<Sampler> m_subsurfaceSampler;
  float3 m_subsurfaceRadius{make_float3(0.1f, 0.1f, 0.1f)};
  float m_subsurfaceScale{0.1f};
  float m_subsurfaceIor{1.4f};
  float m_subsurfaceAnisotropy{0.f};

  // CYCLES_MATERIAL_EMISSIVE_STRENGTH vendor parameter
  float m_emissiveStrength{1.f};

  float m_thickness{0.f};
  float3 m_attenuationColor{make_float3(1.f, 1.f, 1.f)};
  float m_attenuationDistance{INFINITY};

  helium::AlphaMode m_mode{helium::AlphaMode::OPAQUE};
  float m_alphaCutoff{0.5f};
  bool m_warnedOcclusion{false};
};

} // namespace anari_cycles
