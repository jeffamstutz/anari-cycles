// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
// cycles
#include "scene/light.h"
#include "scene/shader.h"
// std
#include <memory>

namespace anari_cycles {

struct Light : public Object
{
  Light(CyclesGlobalState *s, ccl::Light *light);
  ~Light() override;

  static Light *createInstance(std::string_view type, CyclesGlobalState *state);

  virtual void commitParameters() override;
  virtual void finalize() override;

  ccl::Light *cyclesLight() const;
  ccl::Shader *cyclesShader() const;

  virtual math::mat4 xfm() const = 0;

 protected:
  // Give the underlying Cycles light its own unit-emission shader so it is
  // decoupled from scene->default_light (whose stock emission strength is 0).
  // The light's actual color/intensity is applied via ccl::Light::strength,
  // which Cycles multiplies on top of the shader's emission.
  void attachUnitEmissionShader();

  // The light color scaled by a photometric factor, in the form Cycles
  // expects for ccl::Light::strength.
  ccl::float3 scaledColor(float scale) const;

  // Resolve the ANARI area-light 'radiance'/'intensity'/'power' parameter
  // precedence into a radiance value for an emitter of the given area.
  float photometricRadiance(float area);

  ccl::Light *m_cyclesLight{nullptr};
  ccl::Shader *m_cyclesShader{nullptr};

  anari_vec::float3 m_color;
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Light *, ANARI_LIGHT);
