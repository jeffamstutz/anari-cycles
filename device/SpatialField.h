// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "array/Array1D.h"
#include "array/Array3D.h"
#include "array/ObjectArray.h"

#include "Material.h"
// ours
#include "scene/geometry.h"

namespace anari_cycles {

struct SpatialField : public Object
{
  SpatialField(CyclesGlobalState *s);
  ~SpatialField() override;

  static SpatialField *createInstance(
      std::string_view subtype, CyclesGlobalState *s);

  void finalize() override;

  virtual std::unique_ptr<ccl::Geometry> makeCyclesGeometry() = 0;
  virtual box3 bounds() const = 0;
};

// Subtypes ///////////////////////////////////////////////////////////////////

struct StructuredRegularField : public SpatialField
{
  StructuredRegularField(CyclesGlobalState *s);

  void commitParameters() override;
  void finalize() override;

  std::unique_ptr<ccl::Geometry> makeCyclesGeometry() override;

  box3 bounds() const override;
  bool isValid() const override;

  anari_vec::uint3 m_dims{0u};
  anari_vec::float3 m_origin;
  anari_vec::float3 m_spacing;
  anari_vec::float3 m_coordUpperBound;

  std::vector<float> m_generatedCellWidths;
  std::vector<int> m_generatedBlockBounds;
  std::vector<int> m_generatedBlockLevels;
  std::vector<int> m_generatedBlockOffsets;
  std::vector<float> m_generatedBlockScalars;

  helium::IntrusivePtr<Array3D> m_data;
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(
    anari_cycles::SpatialField *, ANARI_SPATIAL_FIELD);
