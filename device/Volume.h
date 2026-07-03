// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "SpatialField.h"

namespace anari_cycles {

struct Volume : public Object
{
  Volume(CyclesGlobalState *s);
  ~Volume() override;

  static Volume *createInstance(std::string_view subtype, CyclesGlobalState *s);

  // Scene-owned geometry node rendered for this volume (may be null when the
  // volume is invalid).
  virtual ccl::Geometry *cyclesGeometry() const = 0;
  virtual box3 bounds() const = 0;
};

// Subtypes ///////////////////////////////////////////////////////////////////

struct TransferFunction1D : public Volume
{
  TransferFunction1D(CyclesGlobalState *s);
  ~TransferFunction1D() override;

  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;

  ccl::Geometry *cyclesGeometry() const override;

  box3 bounds() const override;

 private:
  void syncCyclesMesh();
  void rebuildCyclesShaderGraph();

  helium::ChangeObserverPtr<SpatialField> m_field;

  box3 m_bounds;

  helium::box1 m_valueRange{0.f, 1.f};
  float m_unitDistance{1.f};

  helium::ChangeObserverPtr<Array1D> m_colorData;
  helium::ChangeObserverPtr<Array1D> m_opacityData;

  ccl::Shader *m_shader{nullptr};
  ccl::Mesh *m_mesh{nullptr};
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Volume *, ANARI_VOLUME);
