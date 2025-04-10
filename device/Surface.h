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
  void finalize() override;

  const Geometry *geometry() const;
  const Material *material() const;

  ccl::Geometry *cyclesGeometry() const;

  bool isValid() const override;
  void warnIfUnknownObject() const override;

 private:
  void cleanupCyclesNode();

  helium::ChangeObserverPtr<Geometry> m_geometry;
  helium::IntrusivePtr<Material> m_material;

  ccl::Geometry *m_cyclesGeometryNode{nullptr};
  bool m_geometryHandleChanged{false};
  bool m_materialHandleChanged{false};
};

} // namespace cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(cycles::Surface *, ANARI_SURFACE);
