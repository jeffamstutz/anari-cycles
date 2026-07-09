// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "array/Array3D.h"
// cycles
#include "scene/image.h"

namespace anari_cycles {

// Scalar voxel conversion shared by VolumeImageLoader and dense-grid access
// (isosurface extraction): 'true' when 'type' is a voxel type this device can
// normalize to float.
bool voxelToFloatSupported(ANARIDataType type);

// Convert 'n' scalar voxels starting at element 'offset' of 'src' into
// normalized floats (UFIXED/FIXED types map to [0,1]/[-1,1]).
void convertVoxelsToFloat(
    ANARIDataType type, const void *src, size_t offset, float *dst, size_t n);

// Loads the voxels of a structuredRegular spatial field as a tiled 2D float
// atlas (one Z slice per tile). Cycles removed dense 3D image textures, so
// the shader graph built by StructuredRegularField reconstructs trilinear 3D
// sampling from two bilinear atlas lookups. (filter="cubic" and the 'nanovdb'
// field instead use Cycles' native VDB voxel-attribute path when the build
// has NanoVDB/OpenVDB support; see spatial_field/StructuredRegularField.cpp.)
class VolumeImageLoader : public ccl::ImageLoader
{
 public:
  VolumeImageLoader(Array3D *data, uint32_t tilesX, uint32_t tilesY);
  ~VolumeImageLoader() override;

  bool load_metadata(ccl::ImageMetaData &metadata,
      const ccl::ImageLoaderParams &params,
      ccl::Progress &progress) override;

  bool load_pixels(const ccl::ImageMetaData &metadata, void *pixels) override;

  ccl::string name() const override;

  bool equals(const ccl::ImageLoader &other) const override;

  void cleanup() override;

  bool is_vdb_loader() const override;

 private:
  // Raw pointer (not IntrusivePtr): the StructuredRegularField that created
  // this loader owns both the array reference and the image handle, so the
  // array outlives any load done through this loader. Holding a reference
  // here would keep the array alive past device release and trip helium's
  // leak detection (same pattern as SamplerImageLoader).
  Array3D *m_data{nullptr};
  uint32_t m_dims[3]{0, 0, 0};
  uint32_t m_tilesX{1};
  uint32_t m_tilesY{1};
};

} // namespace anari_cycles
