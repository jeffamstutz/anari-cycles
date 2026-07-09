// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <array>
#include <cstdint>
#include <vector>

namespace anari_cycles {

// Triangle mesh extracted by marchingCubes(): one welded shell per isovalue,
// concatenated into shared arrays. Deliberately free of Cycles/ANARI types so
// the extraction is unit-testable standalone.
struct MarchingCubesMesh
{
  std::vector<std::array<float, 3>> verts;
  std::vector<std::array<float, 3>> normals; // per-vertex, unit length
  std::vector<uint32_t> tris; // 3 vertex indices per triangle
  std::vector<uint32_t> triIsovalue; // isovalue index each triangle belongs to
};

// Classic 256-case marching cubes over a dense scalar grid ('voxels' holds
// dims[0]*dims[1]*dims[2] values, x fastest). Cell corners are the voxel
// samples; vertex positions lie in field space (origin + spacing * ijk).
// Vertices on shared cell edges are welded per isovalue, so each shell is
// watertight and smooth-shades without faceting. Vertex normals come from
// central-difference field gradients, negated so they point toward decreasing
// field values (outward from the enclosed field > isovalue region); triangle
// winding is made consistent with them (counter-clockwise seen from outside).
// Cells containing non-finite voxel values are skipped; isovalues outside the
// data range simply contribute no triangles.
void marchingCubes(const float *voxels,
    const uint32_t dims[3],
    const float origin[3],
    const float spacing[3],
    const std::vector<float> &isovalues,
    MarchingCubesMesh &out);

} // namespace anari_cycles
