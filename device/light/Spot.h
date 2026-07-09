// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Light.h"
// std
#include <cmath>

namespace anari_cycles {

struct Spot : public Light
{
  Spot(CyclesGlobalState *s);

  void commitParameters() override;
  void finalize() override;
  math::mat4 xfm() const override;

 private:
  math::float3 m_position{0.f, 0.f, 0.f};
  math::float3 m_direction{0.f, 0.f, -1.f};
  float m_value{1.f};
  bool m_usesPower{false};
  float m_openingAngle{M_PI};
  float m_falloffAngle{0.1f};
  float m_radius{0.f};
};

} // namespace anari_cycles
