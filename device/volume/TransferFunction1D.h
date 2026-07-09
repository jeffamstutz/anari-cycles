// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "FieldVolume.h"
#include "array/Array1D.h"

namespace anari_cycles {

struct TransferFunction1D : public FieldVolume
{
  TransferFunction1D(CyclesGlobalState *s);
  ~TransferFunction1D() override;

  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;

 private:
  void rebuildCyclesShaderGraph();

  helium::ChangeObserverPtr<SpatialField> m_field;

  helium::box1 m_valueRange{0.f, 1.f};
  float m_unitDistance{1.f};

  helium::ChangeObserverPtr<Array1D> m_colorData;
  helium::ChangeObserverPtr<Array1D> m_opacityData;
};

} // namespace anari_cycles
