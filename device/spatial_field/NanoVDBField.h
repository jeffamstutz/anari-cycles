// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "SpatialField.h"
#include "array/Array1D.h"

namespace anari_cycles {

// KHR_SPATIAL_FIELD_NANOVDB: 'nanovdb' field wrapping a serialized NanoVDB
// grid ('data', a UINT8 array holding the in-memory grid buffer) and sampling
// it through Cycles' native VDB volume path (VDBImageLoader + voxel-grid
// attribute). Requires a build with WITH_CYCLES_NANOVDB/WITH_CYCLES_OPENVDB;
// without them the subtype warns and yields an invalid field.
struct NanoVDBField : public SpatialField
{
  NanoVDBField(CyclesGlobalState *s);
  ~NanoVDBField() override;

  void commitParameters() override;
  void finalize() override;

  ccl::ShaderOutput *createCyclesSamplingNodes(ccl::ShaderGraph *graph) override;

  box3 bounds() const override;
  float stepSize() const override;
  bool isValid() const override;

  // Isosurface support: densely resamples the grid over its index-space
  // bounds. Fails (empty isosurface) for non-axis-aligned grid transforms
  // and for grids whose dense expansion exceeds a sanity cap.
  bool getDenseVoxelGrid(std::vector<float> &voxels,
      anari_vec::uint3 &dims,
      anari_vec::float3 &origin,
      anari_vec::float3 &spacing) const override;

 private:
  helium::ChangeObserverPtr<Array1D> m_data;
  bool m_linearFilter{true};
  box3 m_bounds;
  float m_stepSize{0.f};
};

} // namespace anari_cycles
