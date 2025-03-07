// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
// cycles
#include "scene/light.h"
// std
#include <memory>

namespace cycles {

struct Light : public Object
{
  Light(CyclesGlobalState *s);
  ~Light() override;

  static Light *createInstance(std::string_view type, CyclesGlobalState *state);

  virtual void commitParameters() override;

  std::unique_ptr<ccl::Light> cyclesLight();

 protected:
  ccl::Light *m_cyclesLight{nullptr};

  anari_vec::float3 m_color;
};

} // namespace cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(cycles::Light *, ANARI_LIGHT);
