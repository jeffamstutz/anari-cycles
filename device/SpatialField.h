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

namespace ccl {
class Geometry;
}

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

  // Fields sampled through Cycles' native voxel-grid (VDB) path store their
  // grid image in m_voxelImage; the consuming volume re-attaches it to its
  // geometry here on every finalize (the geometry was just rebuilt, which
  // drops previously attached attributes).
  void attachVoxelAttributes(ccl::Geometry *geom) const;

  // True when this field's voxel-grid lookups need Cycles' shader-wide
  // tricubic volume interpolation (structuredRegular filter="cubic").
  virtual bool cubicVolumeInterpolation() const;

  // True when this field samples through a Cycles voxel-grid attribute (the
  // native VDB path) rather than shader-graph math.
  bool usesVoxelAttributes() const { return !m_voxelImage.empty(); }

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

 protected:
  // Shader-graph side of the voxel-grid path: an AttributeNode bound to
  // m_voxelAttributeName (the kernel samples the attached grid image at the
  // shading position, honoring the image interpolation / cubic shader flag).
  ccl::ShaderOutput *createVoxelSamplingNodes(ccl::ShaderGraph *graph);

  // Grid image sampled through the voxel attribute (empty when this field
  // does not use the VDB path).
  ccl::ImageHandle m_voxelImage;

 private:
  // Unique per-instance attribute name tying createVoxelSamplingNodes'
  // AttributeNode to the image attached by attachVoxelAttributes (a volume
  // shader can sample several fields, e.g. principled density+temperature).
  ccl::ustring m_voxelAttributeName;
};

// Subtypes ///////////////////////////////////////////////////////////////////

struct StructuredRegularField : public SpatialField
{
  StructuredRegularField(CyclesGlobalState *s);
  ~StructuredRegularField() override;

  void commitParameters() override;
  void finalize() override;

  ccl::ShaderOutput *createCyclesSamplingNodes(ccl::ShaderGraph *graph) override;
  bool cubicVolumeInterpolation() const override;

  box3 bounds() const override;
  float stepSize() const override;
  bool isValid() const override;

  bool getDenseVoxelGrid(std::vector<float> &voxels,
      anari_vec::uint3 &dims,
      anari_vec::float3 &origin,
      anari_vec::float3 &spacing) const override;

 private:
  // filter="cubic" (KHR_SPATIAL_FIELD_STRUCTURED_REGULAR_CUBIC): converts the
  // dense voxels to a Cycles VDB grid image sampled with the shader-wide
  // tricubic interpolation flag. Returns false (falling back to the 2D-atlas
  // path) when the build lacks VDB support or the voxel type is unsupported.
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

 private:
  helium::ChangeObserverPtr<Array1D> m_data;
  bool m_linearFilter{true};
  box3 m_bounds;
  float m_stepSize{0.f};
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(
    anari_cycles::SpatialField *, ANARI_SPATIAL_FIELD);
