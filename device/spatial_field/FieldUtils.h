// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CyclesGlobalState.h"
#include "cycles_math.h"
// cycles
#include "scene/image.h"
#include "scene/scene.h"
// std
#include <cstring>
#include <memory>

#if defined(WITH_OPENVDB) && defined(WITH_NANOVDB)
// Cycles' native VDB volume path: VDBImageLoader converts an OpenVDB grid to
// the NanoVDB image sampled by the kernel, so both libraries are required.
#define ANARI_CYCLES_HAS_VDB 1
// cycles
#include "scene/image_vdb.h" // pulls in <nanovdb/NanoVDB.h> + GridHandle
// openvdb/nanovdb
#include <openvdb/openvdb.h>
#if NANOVDB_MAJOR_VERSION_NUMBER > 32 || \
    (NANOVDB_MAJOR_VERSION_NUMBER == 32 && NANOVDB_MINOR_VERSION_NUMBER >= 7)
#include <nanovdb/tools/NanoToOpenVDB.h>
#else
#include <nanovdb/util/NanoToOpenVDB.h>
#endif
#endif

namespace anari_cycles {

// All spatial field images are non-color voxel data; only the loader and the
// interpolation mode differ between the atlas and VDB paths.
inline ccl::ImageHandle addFieldImage(CyclesGlobalState &state,
    std::unique_ptr<ccl::ImageLoader> loader,
    InterpolationType interpolation)
{
  ccl::ImageParams params;
  params.alpha_type = IMAGE_ALPHA_AUTO;
  params.colorspace = ccl::u_colorspace_data;
  params.interpolation = interpolation;
  return state.scene->image_manager->add_image(
      std::move(loader), params, false);
}

#ifdef ANARI_CYCLES_HAS_VDB

// VDBImageLoader with device-controlled precision and no value clipping:
// ANARI fields hand us explicit voxel data, so nothing may be dropped or
// quantized beyond what the input grid already encodes.
class FieldVDBImageLoader : public ccl::VDBImageLoader
{
 public:
  FieldVDBImageLoader(
      openvdb::GridBase::ConstPtr g, const char *name, int gridPrecision)
      : VDBImageLoader(std::move(g), name, 0.f)
  {
    precision = gridPrecision;
  }

  // Dense scalar voxels (structuredRegular filter="cubic"): 'objectToTexture'
  // maps object space to the [0,1]^3 texture space spanning dims*spacing from
  // the field origin, which lands voxel centers on integer grid indices.
  FieldVDBImageLoader(const float *voxels,
      const anari_vec::uint3 &dims,
      const ccl::Transform &objectToTexture,
      const char *name)
      : VDBImageLoader(name, 0.f)
  {
    precision = 32;
    grid_from_dense_voxels(dims[0], dims[1], dims[2], 1, voxels, objectToTexture);
  }
};

inline openvdb::GridBase::Ptr nanovdbGridToOpenVDB(const nanovdb::GridHandle<> &handle)
{
#if NANOVDB_MAJOR_VERSION_NUMBER > 32 || \
    (NANOVDB_MAJOR_VERSION_NUMBER == 32 && NANOVDB_MINOR_VERSION_NUMBER >= 7)
  return nanovdb::tools::nanoToOpenVDB(handle);
#else
  return nanovdb::nanoToOpenVDB(handle);
#endif
}

// Precision VDBImageLoader re-encodes the grid with, chosen to not degrade
// the input: quantized NanoVDB grids stay quantized, everything else keeps
// full float precision.
inline int gridImagePrecision(nanovdb::GridType gridType)
{
  switch (gridType) {
    case nanovdb::GridType::Fp4:
    case nanovdb::GridType::Fp8:
    case nanovdb::GridType::Fp16:
    case nanovdb::GridType::Half:
      return 16;
    case nanovdb::GridType::FpN:
      return 0;
    default:
      return 32;
  }
}

// Copy an ANARI 'data' blob into a NanoVDB-owned buffer and wrap it as a
// grid handle. The copy matters twice over: ANARI arrays guarantee no
// particular alignment while NanoVDB requires NANOVDB_DATA_ALIGNMENT, and it
// decouples downstream use of the grid from the ANARI array's lifetime.
// Throws std::runtime_error when the blob is not a valid NanoVDB grid.
inline nanovdb::GridHandle<> gridHandleFromBlob(const void *data, size_t numBytes)
{
  auto buffer = nanovdb::HostBuffer::create(numBytes);
  std::memcpy(buffer.data(), data, numBytes);
  return nanovdb::GridHandle<>(std::move(buffer));
}

// Scalar dense resampling (isosurface extraction) over the grid types the
// registry's "serialized NanoVDB grid" can reasonably carry.
template <typename BuildT>
void readDenseVoxelsT(const nanovdb::NanoGrid<BuildT> *grid,
    const nanovdb::CoordBBox &bbox,
    float *dst)
{
  auto acc = grid->getAccessor();
  size_t idx = 0;
  // int64 counters: a bbox touching INT32_MAX must not wrap the loop index
  for (int64_t k = bbox.min()[2]; k <= bbox.max()[2]; ++k)
    for (int64_t j = bbox.min()[1]; j <= bbox.max()[1]; ++j)
      for (int64_t i = bbox.min()[0]; i <= bbox.max()[0]; ++i) {
        dst[idx++] = float(
            acc.getValue(nanovdb::Coord(int32_t(i), int32_t(j), int32_t(k))));
      }
}

inline bool readDenseVoxels(
    const nanovdb::GridHandle<> &handle, const nanovdb::CoordBBox &bbox, float *dst)
{
  if (const auto *grid = handle.grid<float>())
    readDenseVoxelsT(grid, bbox, dst);
  else if (const auto *grid = handle.grid<double>())
    readDenseVoxelsT(grid, bbox, dst);
  else if (const auto *grid = handle.grid<nanovdb::Fp4>())
    readDenseVoxelsT(grid, bbox, dst);
  else if (const auto *grid = handle.grid<nanovdb::Fp8>())
    readDenseVoxelsT(grid, bbox, dst);
  else if (const auto *grid = handle.grid<nanovdb::Fp16>())
    readDenseVoxelsT(grid, bbox, dst);
  else if (const auto *grid = handle.grid<nanovdb::FpN>())
    readDenseVoxelsT(grid, bbox, dst);
  else
    return false;
  return true;
}

#endif // ANARI_CYCLES_HAS_VDB

} // namespace anari_cycles
