// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Light.h"
// std
#include <string>

namespace anari_cycles {

struct UnknownLight : public Light
{
  UnknownLight(std::string_view subtype, CyclesGlobalState *s);

  bool isValid() const override;
  void warnIfUnknownObject() const override;
  math::mat4 xfm() const override;

 private:
  std::string m_subtype;
};

} // namespace anari_cycles
