// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Array.h"
#include "Object.h"
// cycles
#include "scene/image.h"
#include "scene/shader_graph.h"
// std
#include <vector>

namespace anari_cycles {

struct SpatialField : public Object
{
  SpatialField(CyclesGlobalState *s);
  ~SpatialField() override;

  static SpatialField *createInstance(
      std::string_view subtype, CyclesGlobalState *s);

  void finalize() override;

  // Add shader nodes to 'graph' that sample this field at the current
  // (object space) shading position. Returns the scalar field value output,
  // or nullptr if sampling nodes cannot be created.
  virtual ccl::ShaderOutput *createCyclesSamplingNodes(
      ccl::ShaderGraph *graph) = 0;

  virtual box3 bounds() const = 0;

  // Suggested object-space ray marching step size.
  virtual float stepSize() const = 0;

  // Dense scalar grid access for isosurface extraction: fills 'dims',
  // 'origin' and 'spacing' and writes dims.x*dims.y*dims.z voxel values
  // (normalized to float, x fastest) into 'voxels'. Returns false when this
  // field cannot provide a dense grid (the isosurface geometry then extracts
  // an empty mesh).
  virtual bool getDenseVoxelGrid(std::vector<float> &voxels,
      anari_vec::uint3 &dims,
      anari_vec::float3 &origin,
      anari_vec::float3 &spacing) const
  {
    return false;
  }
};

// Subtypes ///////////////////////////////////////////////////////////////////

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
  anari_vec::uint3 m_dims{0u};
  anari_vec::float3 m_origin{0.f, 0.f, 0.f};
  anari_vec::float3 m_spacing{1.f, 1.f, 1.f};
  bool m_linearFilter{true};

  // Z slices tiled into a 2D image atlas (see VolumeImageLoader)
  uint32_t m_tilesX{1};
  uint32_t m_tilesY{1};
  ccl::ImageHandle m_atlas;

  helium::ChangeObserverPtr<Array3D> m_data;
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(
    anari_cycles::SpatialField *, ANARI_SPATIAL_FIELD);
