// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"
#include "Material.h"
// cycles
#include "scene/geometry.h"

namespace cycles {

struct Surface : public Object
{
  Surface(CyclesGlobalState *s);
  ~Surface() override;

  void commitParameters() override;

  const Geometry *geometry() const;
  const Material *material() const;

  std::unique_ptr<ccl::Geometry> makeCyclesGeometry();

  bool isValid() const override;
  void warnIfUnknownObject() const override;

 private:
  helium::IntrusivePtr<Geometry> m_geometry;
  helium::IntrusivePtr<Material> m_material;
};

} // namespace cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(cycles::Surface *, ANARI_SURFACE);
