// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Light.h"
#include "array/Array2D.h"

namespace anari_cycles {

struct HDRI : public Light
{
  HDRI(CyclesGlobalState *s);
  ~HDRI() override;

  void commitParameters() override;
  void finalize() override;
  math::mat4 xfm() const override;

  void setCameraBackgroundColor(const math::float3 &color) override;

 private:
  // (Re)build the environment shader graph from the committed parameters
  // and m_cameraBgColor. Keeps the ccl::Shader node itself stable so
  // scene->background's shader pointer stays valid across rebuilds.
  void rebuildEnvironmentShader();

  helium::IntrusivePtr<Array2D> m_radiance{};
  math::float3 m_up{0.f, 0.f, 1.f};
  math::float3 m_direction{1.f, 0.f, 0.f};

  float m_scale{1.f};

  // When not 'visible', camera rays see this solid color instead of the
  // environment; synced to the active renderer's 'background' each frame.
  // Baked into the shader graph as a constant (cached ShaderNode pointers
  // are unsafe: Cycles constant-folds and frees nodes on compile), so a
  // color change rebuilds the graph.
  math::float3 m_cameraBgColor{0.f, 0.f, 0.f};
};

} // namespace anari_cycles
