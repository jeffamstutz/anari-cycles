// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"
// std
#include <string>
#include <vector>

namespace anari_cycles {

// Cycles has no analytic cylinder/cone primitive, so both subtypes tessellate
// every segment into a triangle mesh: an N-gon lateral surface with analytic
// smooth normals plus optional flat end-cap disks. The side count is
// radius-independent; 32 sides keeps silhouettes smooth at typical primitive
// sizes while staying cheap (<= 128 triangles per fully-capped segment).
struct Tube : public Geometry
{
  // Cylinder radii come from 'primitive.radius' (one radius per segment),
  // cone radii from 'vertex.radius' (interpolated along each segment).
  enum class RadiusSource
  {
    PER_PRIMITIVE, // cylinder
    PER_VERTEX // cone
  };

  Tube(CyclesGlobalState *s, RadiusSource radiusSource, const char *subtype);
  ~Tube() override;

  void commitParameters() override;
  void finalize() override;

  ccl::Geometry *createCyclesGeometryNode() override;
  void syncCyclesNode(ccl::Geometry *node) const override;

  box3 bounds() const override;

 private:
  struct TubeMeshData
  {
    std::vector<ccl::float3> verts;
    std::vector<ccl::float3> normals;
    std::vector<uint32_t> srcVertex; // ANARI vertex each generated vertex maps to
    std::vector<uint32_t> srcPrim; // ANARI segment each triangle comes from
    std::vector<uint32_t> tris; // 3 entries per triangle
    std::vector<uint8_t> smooth; // per triangle (bool)
  };

  // Invokes f(prim, v0, v1, p0, p1, length, r0, r1) for every renderable
  // segment. Segments referencing out-of-range vertices and degenerate
  // segments (non-finite or zero length, so bounds() and tessellate() agree
  // on what renders) are skipped; negative radii clamp to 0. Returns the
  // number of skipped segments.
  template <typename F>
  size_t forEachSegment(F &&f) const;

  bool capEnabled(uint64_t vertIdx, bool isFirstVertex) const;
  void tessellate(TubeMeshData &md) const;

  helium::ChangeObserverPtr<Array1D> m_index;
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  helium::ChangeObserverPtr<Array1D> m_radiusArray; // primitive.radius/vertex.radius
  helium::ChangeObserverPtr<Array1D> m_vertexCap;
  float m_radius{1.f};
  std::string m_caps{"none"};
  RadiusSource m_radiusSource{RadiusSource::PER_PRIMITIVE};
  const char *m_subtype{"cylinder"};
};

} // namespace anari_cycles
