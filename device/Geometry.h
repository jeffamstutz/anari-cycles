// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "array/Array1D.h"
// cycles
#include "scene/geometry.h"

namespace cycles {

struct Geometry : public Object {
  Geometry(CyclesGlobalState *s);
  ~Geometry() override;

  static Geometry *createInstance(std::string_view type, CyclesGlobalState *state);

  virtual std::unique_ptr<ccl::Geometry> makeCyclesGeometry() = 0;
};

}  // namespace cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(cycles::Geometry *, ANARI_GEOMETRY);
