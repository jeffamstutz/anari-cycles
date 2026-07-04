// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Array.h"
#include "Object.h"
// cycles
#include "scene/light.h"
#include "scene/shader.h"
// std
#include <memory>
#include <string>
#include <vector>

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

  // Parsed 'intensityDistribution' (KHR quad/ring photometric profiles):
  // nC C-halfplane rows (uniform over [0,2pi), row 0 at C=0) of nV luminous
  // intensity samples over the polar angle gamma, uniform on [0,pi]. Values
  // are dimensionless weights that modulate the light's resolved radiance.
  struct IntensityDistribution
  {
    std::vector<float> values; // nC * nV, C-major (row j = C-plane j)
    int nV{0};
    int nC{0};
    bool present() const { return nV >= 2; }
  };

  // Read + validate the 'intensityDistribution' parameter (ARRAY1D/ARRAY2D
  // of FLOAT32); returns an empty distribution (and warns) when unset or
  // malformed.
  IntensityDistribution getIntensityDistributionParam();

  // Rebuild this light's emission shader graph to apply 'dist' (or restore
  // the plain unit-emission graph when 'dist' is empty). The rows are the
  // light-local -> IES-lookup-vector matrix: the shader dots the local-space
  // emission direction with them so Cycles' IES kernel sees
  // gamma = acos(-z) as the polar angle from the light's emission axis and
  // h = atan2(x, y) + pi as the ANARI C-plane angle (C0 at the subtype's
  // anchor, increasing right-handed around the emission direction).
  void updateEmissionShaderDistribution(const IntensityDistribution &dist,
      const math::float3 &rowX,
      const math::float3 &rowY,
      const math::float3 &rowZ);

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
  // Whether m_cyclesShader currently carries an intensityDistribution graph
  // (so removing the parameter restores the plain unit-emission graph), and
  // the applied graph's inputs (to skip no-op rebuilds on re-commit).
  bool m_shaderHasDistribution{false};
  std::string m_appliedIES;
  math::float3 m_appliedRows[3]{};

  anari_vec::float3 m_color;
  bool m_visible{true};
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Light *, ANARI_LIGHT);
