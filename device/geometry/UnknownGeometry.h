// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"
// std
#include <string>

namespace anari_cycles {

struct UnknownGeometry : public Geometry
{
  UnknownGeometry(std::string_view subtype, CyclesGlobalState *s)
      : Geometry(s), m_subtype(subtype)
  {
    reportMessage(ANARI_SEVERITY_WARNING,
        "created unknown ANARI_GEOMETRY object of subtype '%s'",
        m_subtype.c_str());
  }

  bool isValid() const override
  {
    return false;
  }

  void warnIfUnknownObject() const override
  {
    reportMessage(ANARI_SEVERITY_WARNING,
        "encountered unknown ANARI_GEOMETRY object of subtype '%s'",
        m_subtype.c_str());
  }

  ccl::Geometry *createCyclesGeometryNode() override
  {
    return nullptr;
  }

  void syncCyclesNode(ccl::Geometry *) const override {}

 private:
  std::string m_subtype;
};

} // namespace anari_cycles
