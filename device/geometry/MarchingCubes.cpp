// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "MarchingCubes.h"
#include "MarchingCubesTables.h"
// std
#include <cmath>
#include <cstddef>
#include <unordered_map>
#include <utility>

namespace anari_cycles {

namespace {

using vec3f = std::array<float, 3>;

// Corner c of a cell sits at cell + CORNER_OFFSET[c] (Bourke numbering, see
// MarchingCubesTables.h).
constexpr int CORNER_OFFSET[8][3] = {{0, 0, 0},
    {1, 0, 0},
    {1, 1, 0},
    {0, 1, 0},
    {0, 0, 1},
    {1, 0, 1},
    {1, 1, 1},
    {0, 1, 1}};

// Each cube edge, keyed for welding as (axis, lower grid endpoint): 'axis' is
// the grid axis the edge runs along, (dx,dy,dz) the offset of its lower
// endpoint within the cell. Shared edges of neighboring cells produce the
// same key, which is what welds the mesh.
struct EdgeInfo
{
  int axis;
  int dx, dy, dz;
};
constexpr EdgeInfo EDGE_INFO[12] = {
    {0, 0, 0, 0}, // 0: (0,0,0) -> (1,0,0)
    {1, 1, 0, 0}, // 1: (1,0,0) -> (1,1,0)
    {0, 0, 1, 0}, // 2: (0,1,0) -> (1,1,0)
    {1, 0, 0, 0}, // 3: (0,0,0) -> (0,1,0)
    {0, 0, 0, 1}, // 4: (0,0,1) -> (1,0,1)
    {1, 1, 0, 1}, // 5: (1,0,1) -> (1,1,1)
    {0, 0, 1, 1}, // 6: (0,1,1) -> (1,1,1)
    {1, 0, 0, 1}, // 7: (0,0,1) -> (0,1,1)
    {2, 0, 0, 0}, // 8: (0,0,0) -> (0,0,1)
    {2, 1, 0, 0}, // 9: (1,0,0) -> (1,0,1)
    {2, 1, 1, 0}, // 10: (1,1,0) -> (1,1,1)
    {2, 0, 1, 0} // 11: (0,1,0) -> (0,1,1)
};

} // namespace

void marchingCubes(const float *voxels,
    const uint32_t dims[3],
    const float origin[3],
    const float spacing[3],
    const std::vector<float> &isovalues,
    MarchingCubesMesh &out)
{
  const uint32_t nx = dims[0];
  const uint32_t ny = dims[1];
  const uint32_t nz = dims[2];
  if (nx < 2 || ny < 2 || nz < 2 || !voxels)
    return; // no cells to march

  const size_t numPoints = size_t(nx) * ny * nz;

  auto lin = [&](uint32_t i, uint32_t j, uint32_t k) -> size_t {
    return (size_t(k) * ny + j) * nx + i;
  };

  // Central-difference field gradient at a grid point (one-sided at the
  // borders via index clamping; the divisor uses the actual index span so
  // border gradients stay correctly scaled).
  auto gradientAt = [&](uint32_t i, uint32_t j, uint32_t k) -> vec3f {
    vec3f g = {0.f, 0.f, 0.f};
    const uint32_t ijk[3] = {i, j, k};
    const uint32_t n[3] = {nx, ny, nz};
    for (int a = 0; a < 3; a++) {
      const uint32_t lo = ijk[a] > 0 ? ijk[a] - 1 : 0;
      const uint32_t hi = ijk[a] + 1 < n[a] ? ijk[a] + 1 : n[a] - 1;
      if (lo == hi)
        continue;
      uint32_t pLo[3] = {i, j, k};
      uint32_t pHi[3] = {i, j, k};
      pLo[a] = lo;
      pHi[a] = hi;
      const float dv = voxels[lin(pHi[0], pHi[1], pHi[2])]
          - voxels[lin(pLo[0], pLo[1], pLo[2])];
      const float dx = float(hi - lo) * spacing[a];
      if (dx != 0.f)
        g[a] = dv / dx;
    }
    return g;
  };

  for (uint32_t isoIdx = 0; isoIdx < uint32_t(isovalues.size()); isoIdx++) {
    const float iso = isovalues[isoIdx];

    // One welded vertex per crossed grid edge (per isovalue: nested shells of
    // different isovalues never share vertices). Keyed on (axis, lower
    // endpoint); values index out.verts.
    std::unordered_map<uint64_t, uint32_t> edgeVertex;

    auto edgeVertexIndex = [&](
                               uint32_t ci, uint32_t cj, uint32_t ck, int edge) {
      const EdgeInfo &e = EDGE_INFO[edge];
      const uint32_t i = ci + uint32_t(e.dx);
      const uint32_t j = cj + uint32_t(e.dy);
      const uint32_t k = ck + uint32_t(e.dz);
      const uint64_t key = uint64_t(e.axis) * numPoints + lin(i, j, k);

      auto it = edgeVertex.find(key);
      if (it != edgeVertex.end())
        return it->second;

      const uint32_t i1 = i + uint32_t(e.axis == 0);
      const uint32_t j1 = j + uint32_t(e.axis == 1);
      const uint32_t k1 = k + uint32_t(e.axis == 2);

      const float vA = voxels[lin(i, j, k)];
      const float vB = voxels[lin(i1, j1, k1)];
      // Both endpoints are finite (the cell was screened) and vA != vB when a
      // crossing was detected, but guard the division anyway.
      const float denom = vB - vA;
      const float t = denom != 0.f
          ? std::fmin(std::fmax((iso - vA) / denom, 0.f), 1.f)
          : 0.5f;

      vec3f p = {origin[0] + spacing[0] * float(i),
          origin[1] + spacing[1] * float(j),
          origin[2] + spacing[2] * float(k)};
      p[e.axis] += spacing[e.axis] * t;

      // Normal = -gradient, interpolated between the edge endpoints (points
      // out of the field > isovalue region). Normalized (with a zero-gradient
      // fixup) after extraction.
      const vec3f gA = gradientAt(i, j, k);
      const vec3f gB = gradientAt(i1, j1, k1);
      const vec3f nrm = {-((1.f - t) * gA[0] + t * gB[0]),
          -((1.f - t) * gA[1] + t * gB[1]),
          -((1.f - t) * gA[2] + t * gB[2])};

      const uint32_t idx = uint32_t(out.verts.size());
      out.verts.push_back(p);
      out.normals.push_back(nrm);
      edgeVertex.emplace(key, idx);
      return idx;
    };

    for (uint32_t k = 0; k + 1 < nz; k++) {
      for (uint32_t j = 0; j + 1 < ny; j++) {
        for (uint32_t i = 0; i + 1 < nx; i++) {
          float corner[8];
          bool finite = true;
          for (int c = 0; c < 8; c++) {
            corner[c] = voxels[lin(i + CORNER_OFFSET[c][0],
                j + CORNER_OFFSET[c][1],
                k + CORNER_OFFSET[c][2])];
            finite &= std::isfinite(corner[c]);
          }
          if (!finite)
            continue;

          int cubeIndex = 0;
          for (int c = 0; c < 8; c++) {
            if (corner[c] < iso)
              cubeIndex |= 1 << c;
          }
          if (MC_EDGE_TABLE[cubeIndex] == 0)
            continue;

          const signed char *tt = MC_TRI_TABLE[cubeIndex];
          for (int t = 0; tt[t] != -1; t += 3) {
            const uint32_t a = edgeVertexIndex(i, j, k, tt[t + 0]);
            uint32_t b = edgeVertexIndex(i, j, k, tt[t + 1]);
            uint32_t c = edgeVertexIndex(i, j, k, tt[t + 2]);
            // Welding can collapse table triangles to a degenerate sliver
            // when the isovalue exactly hits a grid sample; drop those.
            if (a == b || b == c || a == c)
              continue;

            // Make the winding agree with the gradient normals so the
            // geometric normal also points out of the field > iso region.
            const vec3f &pa = out.verts[a];
            const vec3f &pb = out.verts[b];
            const vec3f &pc = out.verts[c];
            const vec3f e1 = {
                pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]};
            const vec3f e2 = {
                pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]};
            const vec3f gn = {e1[1] * e2[2] - e1[2] * e2[1],
                e1[2] * e2[0] - e1[0] * e2[2],
                e1[0] * e2[1] - e1[1] * e2[0]};
            const vec3f &na = out.normals[a];
            const vec3f &nb = out.normals[b];
            const vec3f &nc = out.normals[c];
            const float d = gn[0] * (na[0] + nb[0] + nc[0])
                + gn[1] * (na[1] + nb[1] + nc[1])
                + gn[2] * (na[2] + nb[2] + nc[2]);
            if (d < 0.f)
              std::swap(b, c);

            out.tris.push_back(a);
            out.tris.push_back(b);
            out.tris.push_back(c);
            out.triIsovalue.push_back(isoIdx);
          }
        }
      }
    }
  }

  // Normalize the accumulated gradient normals. Zero gradients (field
  // extremum exactly at the isovalue) fall back to the average geometric
  // normal of the incident triangles so shading never sees a NaN.
  std::vector<uint32_t> degenerate;
  for (uint32_t v = 0; v < uint32_t(out.normals.size()); v++) {
    auto &n = out.normals[v];
    const float len =
        std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (len > 1e-12f) {
      n = {n[0] / len, n[1] / len, n[2] / len};
    } else {
      n = {0.f, 0.f, 0.f};
      degenerate.push_back(v);
    }
  }
  if (!degenerate.empty()) {
    std::vector<bool> isDegenerate(out.normals.size(), false);
    for (uint32_t v : degenerate)
      isDegenerate[v] = true;
    auto isDegen = [&](uint32_t v) { return bool(isDegenerate[v]); };
    for (size_t t = 0; t < out.tris.size(); t += 3) {
      const uint32_t a = out.tris[t], b = out.tris[t + 1], c = out.tris[t + 2];
      if (!isDegen(a) && !isDegen(b) && !isDegen(c))
        continue;
      const vec3f &pa = out.verts[a];
      const vec3f &pb = out.verts[b];
      const vec3f &pc = out.verts[c];
      const vec3f e1 = {pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]};
      const vec3f e2 = {pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]};
      const vec3f gn = {e1[1] * e2[2] - e1[2] * e2[1],
          e1[2] * e2[0] - e1[0] * e2[2],
          e1[0] * e2[1] - e1[1] * e2[0]};
      for (uint32_t v : {a, b, c}) {
        if (!isDegen(v))
          continue;
        auto &n = out.normals[v];
        n = {n[0] + gn[0], n[1] + gn[1], n[2] + gn[2]};
      }
    }
    for (uint32_t v : degenerate) {
      auto &n = out.normals[v];
      const float len =
          std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
      n = len > 1e-12f ? vec3f{n[0] / len, n[1] / len, n[2] / len}
                       : vec3f{0.f, 0.f, 1.f};
    }
  }
}

} // namespace anari_cycles
