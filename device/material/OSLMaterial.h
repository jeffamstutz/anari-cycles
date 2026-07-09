// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Material.h"

namespace anari_cycles {

// OSLMaterial ////////////////////////////////////////////////////////////////
//
// CYCLES_MATERIAL_OSL: 'osl' material subtype accepting Open Shading Language
// shaders. 'bytecode' (STRING) holds precompiled .oso text and takes
// precedence over 'source' (STRING), which holds OSL source compiled with
// oslc at commit time (cached under the system temp dir keyed by a content
// hash). The compiled shader's input parameters become settable via equally
// named ANARI parameters chosen by socket type: FLOAT32 (float), INT32 (int),
// FLOAT32_VEC3 (color/point/vector/normal) and STRING (string); unset inputs
// keep the shader's declared defaults. The first closure output is wired to
// the surface output (surface shaders only). Needs a build with
// WITH_CYCLES_OSL=ON *and* Cycles' OSL shading system active (CPU or OptiX
// device -- the shading system is global per session, see
// CyclesDevice::initDevice()); otherwise committing warns and the material
// is invalid (surfaces using it are skipped).

struct OSLMaterial : public Material
{
  OSLMaterial(CyclesGlobalState *s);
  ~OSLMaterial() override = default;

  void commitParameters() override;
  void finalize() override;

  bool isValid() const override;

 private:
  std::string m_source;
  std::string m_bytecode;
  bool m_oslValid{false};
  bool m_warnedUnsupported{false};
};

} // namespace anari_cycles
