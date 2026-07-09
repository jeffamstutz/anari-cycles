// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "FieldVolume.h"

namespace anari_cycles {

// CYCLES_VOLUME_PRINCIPLED: 'principled' volume subtype driving Cycles'
// Principled Volume closure (scatter + absorption + emission/blackbody) with
// the density sampled from a spatial field -- richer than transferFunction1D
// for smoke/fire.
struct PrincipledVolume : public FieldVolume
{
  PrincipledVolume(CyclesGlobalState *s);
  ~PrincipledVolume() override;

  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;

 private:
  void rebuildCyclesShaderGraph();

  helium::ChangeObserverPtr<SpatialField> m_field;
  helium::ChangeObserverPtr<SpatialField> m_temperatureField;

  float m_densityScale{1.f};
  float3 m_color{make_float3(0.5f, 0.5f, 0.5f)};
  float m_anisotropy{0.f};
  float3 m_absorptionColor{make_float3(0.f, 0.f, 0.f)};
  float m_emissionStrength{0.f};
  float3 m_emissionColor{make_float3(1.f, 1.f, 1.f)};
  float m_blackbodyIntensity{0.f};
  float3 m_blackbodyTint{make_float3(1.f, 1.f, 1.f)};
  float m_temperature{1000.f};
};

} // namespace anari_cycles
