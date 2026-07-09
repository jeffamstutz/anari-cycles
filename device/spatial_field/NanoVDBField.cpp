// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "NanoVDBField.h"
#include "FieldUtils.h"
// cycles
#include "scene/geometry.h"
#include "scene/scene.h"
#include "scene/shader_nodes.h"
// std
#include <algorithm>
#include <cmath>
#include <cstring>

namespace anari_cycles {

NanoVDBField::NanoVDBField(CyclesGlobalState *s)
    : SpatialField(s), m_data(this), m_bounds(empty_box3())
{}

NanoVDBField::~NanoVDBField() = default;

void NanoVDBField::commitParameters()
{
  m_data = getParamObject<Array1D>("data");
  m_linearFilter = getParamString("filter", "linear") != "nearest";
}

void NanoVDBField::finalize()
{
  m_voxelImage = ccl::ImageHandle();
  m_bounds = empty_box3();
  m_stepSize = 0.f;

  if (!m_data) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'data' on 'nanovdb' field");
    SpatialField::finalize();
    return;
  }

  if (m_data->elementType() != ANARI_UINT8) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'data' on 'nanovdb' field must be an ANARI_UINT8 array "
        "(a serialized NanoVDB grid)");
    SpatialField::finalize();
    return;
  }

#ifndef ANARI_CYCLES_HAS_VDB
  reportMessage(ANARI_SEVERITY_WARNING,
      "'nanovdb' field requires a device built with WITH_CYCLES_NANOVDB=ON "
      "and WITH_CYCLES_OPENVDB=ON");
#else
  openvdb::initialize();

  try {
    const nanovdb::GridHandle<> handle =
        gridHandleFromBlob(m_data->data(), m_data->size());

    const nanovdb::GridMetaData *metadata = handle.gridMetaData(0);
    if (!metadata || !metadata->isValid()) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'data' on 'nanovdb' field is not a valid serialized NanoVDB grid");
      SpatialField::finalize();
      return;
    }
    if (handle.gridCount() > 1) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'data' on 'nanovdb' field holds %u grids; using the first",
          handle.gridCount());
    }
    if (metadata->isEmpty()) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'data' on 'nanovdb' field has no active voxels");
      SpatialField::finalize();
      return;
    }

    if (metadata->hasBBox()) {
      const auto &worldBounds = metadata->worldBBox();
      m_bounds.lower[0] = float(worldBounds.min()[0]);
      m_bounds.lower[1] = float(worldBounds.min()[1]);
      m_bounds.lower[2] = float(worldBounds.min()[2]);
      m_bounds.upper[0] = float(worldBounds.max()[0]);
      m_bounds.upper[1] = float(worldBounds.max()[1]);
      m_bounds.upper[2] = float(worldBounds.max()[2]);
    } else {
      // Grids serialized without stats lack the precomputed world-space
      // bounds; derive them by mapping the index-space bounds (expanded to
      // voxel corners) through the grid transform.
      const auto &indexBounds = metadata->indexBBox();
      const double lo[3] = {double(indexBounds.min()[0]),
          double(indexBounds.min()[1]),
          double(indexBounds.min()[2])};
      const double hi[3] = {double(indexBounds.max()[0]) + 1.0,
          double(indexBounds.max()[1]) + 1.0,
          double(indexBounds.max()[2]) + 1.0};
      for (int corner = 0; corner < 8; ++corner) {
        const nanovdb::Vec3d p = metadata->map().applyMap(
            nanovdb::Vec3d(corner & 1 ? hi[0] : lo[0],
                corner & 2 ? hi[1] : lo[1],
                corner & 4 ? hi[2] : lo[2]));
        for (int axis = 0; axis < 3; ++axis) {
          m_bounds.lower[axis] = std::min(m_bounds.lower[axis], float(p[axis]));
          m_bounds.upper[axis] = std::max(m_bounds.upper[axis], float(p[axis]));
        }
      }
    }

    const auto voxelSize = metadata->voxelSize();
    m_stepSize = float(std::min({voxelSize[0], voxelSize[1], voxelSize[2]}));

    // Cycles samples VDB volumes from a NanoVDB image built out of an OpenVDB
    // grid (VDBImageLoader), so round-trip the input grid through OpenVDB.
    openvdb::GridBase::Ptr grid = nanovdbGridToOpenVDB(handle);
    if (!grid) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "unsupported NanoVDB grid type on 'nanovdb' field");
      SpatialField::finalize();
      return;
    }

    m_voxelImage = addFieldImage(*deviceState(),
        std::make_unique<FieldVDBImageLoader>(
            grid, "ANARI nanovdb", gridImagePrecision(metadata->gridType())),
        m_linearFilter ? INTERPOLATION_LINEAR : INTERPOLATION_CLOSEST);
  } catch (const std::exception &e) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'data' on 'nanovdb' field is not a valid serialized NanoVDB grid: %s",
        e.what());
  }
#endif

  SpatialField::finalize();
}

ccl::ShaderOutput *NanoVDBField::createCyclesSamplingNodes(
    ccl::ShaderGraph *graph)
{
  return createVoxelSamplingNodes(graph);
}

box3 NanoVDBField::bounds() const
{
  return m_bounds;
}

float NanoVDBField::stepSize() const
{
  return m_stepSize;
}

bool NanoVDBField::isValid() const
{
  return m_data && !m_voxelImage.empty();
}

bool NanoVDBField::getDenseVoxelGrid(std::vector<float> &voxels,
    anari_vec::uint3 &dims,
    anari_vec::float3 &origin,
    anari_vec::float3 &spacing) const
{
#ifdef ANARI_CYCLES_HAS_VDB
  // Like StructuredRegularField, work from the committed 'data' blob rather
  // than finalized state: a consumer's finalize can run before this field's
  // within one commit flush (both are priority-0 objects).
  if (!m_data || m_data->elementType() != ANARI_UINT8
      || m_data->size() < sizeof(nanovdb::GridData)) {
    return false;
  }

  try {
    const nanovdb::GridHandle<> handle =
        gridHandleFromBlob(m_data->data(), m_data->size());
    const nanovdb::GridMetaData *metadata = handle.gridMetaData(0);
    if (!metadata || !metadata->isValid() || metadata->isEmpty())
      return false;

    const auto &indexBounds = metadata->indexBBox();
    const uint64_t n[3] = {
        uint64_t(int64_t(indexBounds.max()[0]) - indexBounds.min()[0] + 1),
        uint64_t(int64_t(indexBounds.max()[1]) - indexBounds.min()[1] + 1),
        uint64_t(int64_t(indexBounds.max()[2]) - indexBounds.min()[2] + 1)};

    // Marching cubes needs the grid densified; cap the expansion so a large
    // but sparse grid cannot trigger an unbounded allocation (64M voxels =
    // 256 MB of floats, roughly a 400^3 dense grid). Checked factor by
    // factor: each term stays <= 2^52, so the running product cannot wrap
    // uint64 and sneak a pathological index bbox (up to 2^32 per axis)
    // under the cap.
    constexpr uint64_t kMaxDenseVoxels = uint64_t(1) << 26;
    if (n[0] > kMaxDenseVoxels || n[1] > kMaxDenseVoxels
        || n[0] * n[1] > kMaxDenseVoxels
        || n[0] * n[1] * n[2] > kMaxDenseVoxels) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "isosurface over 'nanovdb' field needs a %zux%zux%zu dense "
          "grid, exceeding the %zu-voxel cap; extracting an empty surface",
          size_t(n[0]),
          size_t(n[1]),
          size_t(n[2]),
          size_t(kMaxDenseVoxels));
      return false;
    }
    const uint64_t numVoxels = n[0] * n[1] * n[2];

    // The dense grid is described by per-axis origin/spacing, so the grid
    // transform must be axis-aligned: each unit index step may only move
    // along its own world axis.
    const auto &map = metadata->map();
    const nanovdb::Vec3d indexLower(double(indexBounds.min()[0]),
        double(indexBounds.min()[1]),
        double(indexBounds.min()[2]));
    const nanovdb::Vec3d base = map.applyMap(indexLower);
    double axisSpacing[3];
    for (int axis = 0; axis < 3; ++axis) {
      nanovdb::Vec3d p = indexLower;
      p[axis] += 1.0;
      const nanovdb::Vec3d step = map.applyMap(p) - base;
      axisSpacing[axis] = step[axis];
      const double offAxis = std::max(std::abs(step[(axis + 1) % 3]),
          std::abs(step[(axis + 2) % 3]));
      if (offAxis > 1e-5 * std::max(std::abs(step[axis]), 1e-20)) {
        reportMessage(ANARI_SEVERITY_WARNING,
            "isosurface over 'nanovdb' field with a non-axis-aligned grid "
            "transform is not supported; extracting an empty surface");
        return false;
      }
    }

    dims = {uint32_t(n[0]), uint32_t(n[1]), uint32_t(n[2])};
    origin = {float(base[0]), float(base[1]), float(base[2])};
    spacing = {
        float(axisSpacing[0]), float(axisSpacing[1]), float(axisSpacing[2])};

    voxels.resize(numVoxels);
    if (!readDenseVoxels(handle, indexBounds, voxels.data())) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "isosurface over 'nanovdb' field with an unsupported grid value "
          "type; extracting an empty surface");
      return false;
    }
    return true;
  } catch (const std::exception &e) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "isosurface over 'nanovdb' field failed to read the grid (%s); "
        "extracting an empty surface",
        e.what());
    return false;
  }
#else
  (void)voxels;
  (void)dims;
  (void)origin;
  (void)spacing;
  return false;
#endif
}

} // namespace anari_cycles
