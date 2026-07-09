// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Sampler.h"
// std
#include <string>

namespace anari_cycles {

struct UnknownSampler : public Sampler
{
  UnknownSampler(std::string_view subtype, CyclesGlobalState *s)
      : Sampler(s), m_subtype(subtype)
  {
    if (m_subtype == "image3D") {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'image3D' samplers are not supported by the cycles device (Cycles "
          "has no dense 3D image textures); the sampler is treated as an "
          "unknown object");
    } else {
      reportMessage(ANARI_SEVERITY_WARNING,
          "created unknown ANARI_SAMPLER object of subtype '%s'",
          m_subtype.c_str());
    }
  }

  bool isValid() const override
  {
    return false;
  }

  void warnIfUnknownObject() const override
  {
    reportMessage(ANARI_SEVERITY_WARNING,
        "encountered unknown ANARI_SAMPLER object of subtype '%s'",
        m_subtype.c_str());
  }

 private:
  std::string m_subtype;
};

} // namespace anari_cycles
