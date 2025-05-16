// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

namespace anari_cycles {

struct Renderer : public Object
{
  Renderer(CyclesGlobalState *s);
  ~Renderer() override;

  void commitParameters() override;

  void makeRendererCurrent() const;

  bool runAsync() const;

 private:
  anari_vec::float4 m_backgroundColor;
  anari_vec::float3 m_ambientColor;
  float m_ambientIntensity;
  bool m_runAsync{false};
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Renderer *, ANARI_RENDERER);
