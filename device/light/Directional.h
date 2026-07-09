// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Light.h"

namespace anari_cycles {

struct Directional : public Light
{
  Directional(CyclesGlobalState *s);

  void commitParameters() override;
  void finalize() override;
  math::mat4 xfm() const override;

 private:
  math::float3 m_direction{0.f, 0.f, -1.f};
  float m_angularDiameter{0.f};
  float m_strengthValue{1.f};
  bool m_usesRadiance{false};
};

} // namespace anari_cycles
