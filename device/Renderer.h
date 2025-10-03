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

  void makeRendererCurrent();

  bool runAsync() const;

 private:
  struct {
    int background : 1;
    int ambientLight : 1;
  } m_needsUpdateStatus = {true, true};

  math::float4 m_backgroundColor;
  math::float3 m_ambientColor;
  float m_ambientIntensity;
  bool m_runAsync{false};

  void rebuildDefaultLightShader();
  void rebuildDefaultBackgroundShader();
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Renderer *, ANARI_RENDERER);
