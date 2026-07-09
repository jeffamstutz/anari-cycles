// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Light.h"

namespace anari_cycles {

struct Point : public Light
{
  Point(CyclesGlobalState *s);

  void commitParameters() override;
  void finalize() override;
  math::mat4 xfm() const override;

 private:
  enum class Quantity { RADIANCE, INTENSITY, POWER };
  math::float3 m_position{0.f, 0.f, 0.f};
  Quantity m_quantity{Quantity::INTENSITY};
  float m_value{1.f};
  float m_radius{0.f};
};

} // namespace anari_cycles
