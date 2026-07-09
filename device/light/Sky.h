// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Light.h"

namespace anari_cycles {

// CYCLES_LIGHT_SKY: procedural sun+sky dome (Cycles SkyTextureNode, Nishita
// multiple-scattering model) driving scene->background like an HDRI light.
struct Sky : public Light
{
  Sky(CyclesGlobalState *s);
  ~Sky() override;

  void commitParameters() override;
  void finalize() override;
  math::mat4 xfm() const override;

  void setCameraBackgroundColor(const math::float3 &color) override;

 private:
  // (Re)build the sky shader graph from the committed parameters and
  // m_cameraBgColor; keeps the ccl::Shader node itself stable so
  // scene->background's shader pointer stays valid across rebuilds (same
  // scheme as HDRI::rebuildEnvironmentShader()).
  void rebuildSkyShader();

  math::float3 m_sunDirection{0.f, 0.f, 1.f};
  bool m_sunDisc{true};
  float m_sunSize{0.009512f};
  float m_sunIntensity{1.f};
  float m_scale{1.f};
  float m_altitude{0.f};
  float m_airDensity{1.f};
  float m_dustDensity{1.f};
  float m_ozoneDensity{1.f};

  // See HDRI::m_cameraBgColor -- same role for an invisible sky.
  math::float3 m_cameraBgColor{0.f, 0.f, 0.f};
};

} // namespace anari_cycles
