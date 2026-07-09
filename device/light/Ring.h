// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Light.h"
// std
#include <cmath>

namespace anari_cycles {

struct Ring : public Light
{
  Ring(CyclesGlobalState *s);

  void commitParameters() override;
  void finalize() override;
  math::mat4 xfm() const override;

 private:
  math::float3 m_position{0.f, 0.f, 0.f};
  math::float3 m_direction{0.f, 0.f, -1.f};
  math::float3 m_c0{1.f, 0.f, 0.f};
  float m_openingAngle{M_PI};
  float m_radius{0.f};
  float m_effectiveRadius{0.f};
  float m_innerRadius{0.f};
  float m_radiance{1.f};
  bool m_falloffAngleSet{false};
  IntensityDistribution m_distribution;
};

} // namespace anari_cycles
