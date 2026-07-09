// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "SpatialField.h"
#include "array/Array3D.h"

namespace anari_cycles {

struct StructuredRegularField : public SpatialField
{
  StructuredRegularField(CyclesGlobalState *s);
  ~StructuredRegularField() override;

  void commitParameters() override;
  void finalize() override;

  ccl::ShaderOutput *createCyclesSamplingNodes(ccl::ShaderGraph *graph) override;

  box3 bounds() const override;
  float stepSize() const override;
  bool isValid() const override;

  bool getDenseVoxelGrid(std::vector<float> &voxels,
      anari_vec::uint3 &dims,
      anari_vec::float3 &origin,
      anari_vec::float3 &spacing) const override;

 private:
  // filter="cubic" (KHR_SPATIAL_FIELD_STRUCTURED_REGULAR_CUBIC): converts the
  // dense voxels to a Cycles VDB grid image whose per-image interpolation is
  // tricubic. Returns false (falling back to the 2D-atlas path) when the
  // build lacks VDB support or the voxel type is unsupported.
  bool finalizeCubicGrid();
  ccl::ShaderOutput *createAtlasSamplingNodes(ccl::ShaderGraph *graph);

  enum class Filter
  {
    NEAREST,
    LINEAR,
    CUBIC
  };

  anari_vec::uint3 m_dims{0u};
  anari_vec::float3 m_origin{0.f, 0.f, 0.f};
  anari_vec::float3 m_spacing{1.f, 1.f, 1.f};
  Filter m_filter{Filter::LINEAR};

  // Z slices tiled into a 2D image atlas (see VolumeImageLoader); used for
  // nearest/linear filtering (and as the cubic fallback).
  uint32_t m_tilesX{1};
  uint32_t m_tilesY{1};
  ccl::ImageHandle m_atlas;

  helium::ChangeObserverPtr<Array3D> m_data;
};

} // namespace anari_cycles
