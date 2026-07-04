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

  // Some subtypes need a second Cycles emitter (quad side='both'); nullptr
  // for everything else. When non-null, instancing code must place it with
  // secondaryXfm() next to the primary light.
  virtual ccl::Light *secondaryCyclesLight() const;
  virtual math::mat4 secondaryXfm() const;

  // KHR_AREA_LIGHTS 'visible': whether camera rays see the light geometry.
  bool visibleToCamera() const;

  // HDRI lights drive scene->background; when not 'visible' their shader
  // shows a solid color to camera rays which must track the active
  // renderer's 'background' parameter. No-op for all other subtypes.
  virtual void setCameraBackgroundColor(const math::float3 &color);

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

  // Fetch a direction parameter and normalize it, warning and substituting
  // 'fallback' (assumed unit length) when it is zero-length or non-finite.
  math::float3 getNormalizedDirection(
      const char *name, const math::float3 &fallback);

  ccl::Light *m_cyclesLight{nullptr};
  ccl::Shader *m_cyclesShader{nullptr};

  anari_vec::float3 m_color;
  bool m_visible{true};
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Light *, ANARI_LIGHT);
