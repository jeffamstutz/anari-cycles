// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

namespace anari_cycles {

struct Sampler : public Object
{
  Sampler(CyclesGlobalState *s);
  ~Sampler() override;

  static Sampler *createInstance(
      std::string_view subtype, CyclesGlobalState *s);
};

} // namespace anari_cycles
