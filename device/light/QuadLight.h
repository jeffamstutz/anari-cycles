// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Light.h"
// std
#include <string>

namespace anari_cycles {

struct QuadLight : public Light
{
  QuadLight(CyclesGlobalState *s);
  ~QuadLight() override;

  bool isValid() const override;
  void commitParameters() override;
  void finalize() override;
  math::mat4 xfm() const override;
  ccl::Light *secondaryCyclesLight() const override;
  math::mat4 secondaryXfm() const override;

 private:
  math::mat4 quadXfm(bool backSide) const;

  // Second emitter for side='both' (a Cycles area light only emits from one
  // hemisphere); shares the unit-emission shader, only instanced when used.
  ccl::Light *m_cyclesLightBack{nullptr};

  math::float3 m_position{0.f, 0.f, 0.f};
  math::float3 m_edge1{1.f, 0.f, 0.f};
  math::float3 m_edge2{0.f, 1.f, 0.f};
  float m_area{1.f};
  float m_radiance{1.f};
  std::string m_side{"front"};
  IntensityDistribution m_distribution;
};

} // namespace anari_cycles
