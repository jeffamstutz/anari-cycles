// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Tube.h"
#include "GeometryAttributes.h"
// cycles
#include "scene/mesh.h"
// std
#include <algorithm>
#include <cmath>

namespace anari_cycles {

// The side count is radius-independent; 32 sides keeps silhouettes smooth at
// typical primitive sizes while staying cheap (<= 128 triangles per
// fully-capped segment). See the tessellation overview in Tube.h.
static constexpr uint32_t TUBE_NUM_SIDES = 32;

Tube::Tube(CyclesGlobalState *s, RadiusSource radiusSource, const char *subtype)
    : Geometry(s),
      m_index(this),
      m_vertexPosition(this),
      m_radiusArray(this),
      m_vertexCap(this),
      m_radiusSource(radiusSource),
      m_subtype(subtype)
{}

Tube::~Tube() = default;

void Tube::commitParameters()
{
  Geometry::commitParameters();
  commitAttributeParameters();

  m_index = getParamObject<Array1D>("primitive.index");
  if (m_index) {
    const anari::DataType t = m_index->elementType();
    if (t != ANARI_UINT32_VEC2 && t != ANARI_UINT64_VEC2) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'primitive.index' on %s geometry must be an array of UINT32_VEC2 "
          "or UINT64_VEC2 (got %s) -- ignoring",
          m_subtype,
          anari::toString(t));
      m_index = nullptr;
    }
  }
  m_vertexPosition = validatedVertexPosition(m_subtype).ptr;
  m_radiusArray = getParamObject<Array1D>(
      m_radiusSource == RadiusSource::PER_PRIMITIVE ? "primitive.radius"
                                                    : "vertex.radius");
  m_vertexCap = getParamObject<Array1D>("vertex.cap");
  m_radius = getParam<float>("radius", 1.f);
  m_caps = getParamString("caps", "none");
}

void Tube::finalize()
{
  if (!m_vertexPosition) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'vertex.position' on %s geometry",
        m_subtype);
  }

  Geometry::finalize();
}

ccl::Geometry *Tube::createCyclesGeometryNode()
{
  return deviceState()->scene->create_node<ccl::Mesh>();
}

void Tube::syncCyclesNode(ccl::Geometry *node) const
{
  auto *mesh = (ccl::Mesh *)node;

  // With no positions the mesh syncs empty (rather than early-returning) so
  // a previously synced tessellation cannot outlive the removal of
  // 'vertex.position' — bounds() reports this geometry as empty.
  TubeMeshData md;
  if (m_vertexPosition) {
    tessellate(md);
  } else {
    reportMessage(ANARI_SEVERITY_WARNING,
        "Tube::syncCyclesNode() detected incomplete %s geometry",
        m_subtype);
  }

  const size_t numVerts = md.verts.size();
  const size_t numTris = md.tris.size() / 3;

  ccl::array<ccl::float3> P;
  auto *dstP = P.resize(numVerts);
  std::copy(md.verts.begin(), md.verts.end(), dstP);
  mesh->set_verts(P);

  mesh->resize_mesh(numVerts, numTris);
  auto *triangles = mesh->get_triangles().data();
  auto *shader = mesh->get_shader().data();
  auto *smooth = mesh->get_smooth().data();
  std::copy(md.tris.begin(), md.tris.end(), triangles);
  for (size_t i = 0; i < numTris; i++) {
    shader[i] = 0;
    smooth[i] = md.smooth[i];
  }
  mesh->tag_triangles_modified();
  mesh->tag_shader_modified();
  mesh->tag_smooth_modified();

  // Analytic normals: radial (tilted by the cone slope) on the lateral
  // surface. Cap triangles are flat-shaded, so their vertex normals (set to
  // the cap plane normal) are ignored in favor of the geometric normal.
  {
    Attribute *attr =
        mesh->attributes.add(ATTR_STD_VERTEX_NORMAL, ustring("vertex.normal"));
    packed_normal *dst = attr->data_normal_for_write();
    for (size_t i = 0; i < numVerts; i++)
      dst[i] = packed_normal(md.normals[i]);
    attr->modified = true;
  }

  // Each generated vertex inherits the attributes of its source ANARI vertex,
  // each generated triangle those of its source ANARI segment.
  auto &attrs = mesh->attributes;
  auto vertexOf = [&](size_t i) -> size_t { return md.srcVertex[i]; };
  auto primOf = [&](size_t t) -> size_t { return md.srcPrim[t]; };

  for (int c = 0; c < NUM_ATTRIBUTE_CHANNELS; c++) {
    if (m_vertexAttr[c]) {
      writeAttributeArray(
          attrs, c, ATTR_ELEMENT_VERTEX, numVerts, *m_vertexAttr[c], vertexOf);
    } else if (m_primitiveAttr[c]) {
      writeAttributeArray(
          attrs, c, ATTR_ELEMENT_FACE, numTris, *m_primitiveAttr[c], primOf);
    } else if (m_uniformAttr[c]) {
      writeAttributeConstant(attrs, c, *m_uniformAttr[c]);
    } else if (c == CH_COLOR) {
      writeAttributeConstant(attrs, c, DEFAULT_COLOR);
    } else {
      attrs.remove(ustring(CHANNEL_CYCLES_NAME[c]));
    }
  }

  writePrimitiveId(attrs, ATTR_ELEMENT_FACE, numTris, m_primitiveId.get(), primOf);
}

box3 Tube::bounds() const
{
  box3 b = empty_box3();
  if (!m_vertexPosition)
    return b;

  forEachSegment([&](size_t,
                     uint64_t,
                     uint64_t,
                     const float3 &p0,
                     const float3 &p1,
                     float,
                     float r0,
                     float r1) {
    extend(b, p0 - make_float3(r0));
    extend(b, p0 + make_float3(r0));
    extend(b, p1 - make_float3(r1));
    extend(b, p1 + make_float3(r1));
  });
  return b;
}

template <typename F>
size_t Tube::forEachSegment(F &&f) const
{
  const size_t numVerts = m_vertexPosition->size();
  const size_t numSegments = m_index ? m_index->size() : numVerts / 2;
  const auto *srcPos = m_vertexPosition->beginAs<anari_vec::float3>();

  const uint32_t *idx32 = nullptr;
  const uint64_t *idx64 = nullptr;
  if (m_index) {
    if (m_index->elementType() == ANARI_UINT64_VEC2)
      idx64 = (const uint64_t *)m_index->begin();
    else // ANARI_UINT32_VEC2
      idx32 = (const uint32_t *)m_index->begin();
  }

  const float *radiusArray =
      m_radiusArray ? m_radiusArray->beginAs<float>() : nullptr;
  const size_t radiusCount = m_radiusArray ? m_radiusArray->size() : 0;

  size_t numSkipped = 0;
  for (size_t i = 0; i < numSegments; i++) {
    uint64_t v0, v1;
    if (idx64) {
      v0 = idx64[2 * i + 0];
      v1 = idx64[2 * i + 1];
    } else if (idx32) {
      v0 = idx32[2 * i + 0];
      v1 = idx32[2 * i + 1];
    } else {
      v0 = 2 * i + 0;
      v1 = 2 * i + 1;
    }

    // srcVertex/attribute remapping stores 32-bit vertex ids, so indices
    // beyond UINT32_MAX are rejected along with out-of-range ones.
    if (v0 >= numVerts || v1 >= numVerts || v0 > UINT32_MAX
        || v1 > UINT32_MAX) {
      numSkipped++;
      continue;
    }

    const float3 p0 = make_float3(srcPos[v0][0], srcPos[v0][1], srcPos[v0][2]);
    const float3 p1 = make_float3(srcPos[v1][0], srcPos[v1][1], srcPos[v1][2]);
    const float L = len(p1 - p0);
    if (!(L > 0.f) || !std::isfinite(L)) { // catches zero-length and NaN/inf
      numSkipped++;
      continue;
    }

    float r0, r1;
    if (m_radiusSource == RadiusSource::PER_PRIMITIVE) {
      r0 = r1 = (radiusArray && i < radiusCount) ? radiusArray[i] : m_radius;
    } else {
      r0 = (radiusArray && v0 < radiusCount) ? radiusArray[v0] : m_radius;
      r1 = (radiusArray && v1 < radiusCount) ? radiusArray[v1] : m_radius;
    }
    r0 = std::max(r0, 0.f);
    r1 = std::max(r1, 0.f);

    f(i, v0, v1, p0, p1, L, r0, r1);
  }

  return numSkipped;
}

bool Tube::capEnabled(uint64_t vertIdx, bool isFirstVertex) const
{
  // A 'vertex.cap' array overrides the global 'caps' string (0 = no cap,
  // nonzero = flat cap); fall back to 'caps' for vertices it doesn't cover.
  if (m_vertexCap && vertIdx < m_vertexCap->size())
    return m_vertexCap->beginAs<uint8_t>()[vertIdx] != 0;
  return isFirstVertex ? (m_caps == "first" || m_caps == "both")
                       : (m_caps == "second" || m_caps == "both");
}

void Tube::tessellate(TubeMeshData &md) const
{
  constexpr uint32_t N = TUBE_NUM_SIDES;

  // Unit cross-section directions, shared by all segments (in each segment's
  // local frame).
  float2 ring[N];
  for (uint32_t j = 0; j < N; j++) {
    const float theta = (float(j) / float(N)) * M_2PI_F;
    ring[j] = make_float2(cosf(theta), sinf(theta));
  }

  // Worst-case sizing (all segments valid, caps only when configured) to
  // avoid reallocation-and-copy churn on large inputs.
  {
    const size_t numSegments =
        m_index ? m_index->size() : m_vertexPosition->size() / 2;
    const bool capsPossible = m_vertexCap || m_caps != "none";
    const size_t vertsPerSeg = 2 * N + (capsPossible ? 2 * (N + 1) : 0);
    const size_t trisPerSeg = 2 * N + (capsPossible ? 2 * N : 0);
    md.verts.reserve(numSegments * vertsPerSeg);
    md.normals.reserve(numSegments * vertsPerSeg);
    md.srcVertex.reserve(numSegments * vertsPerSeg);
    md.srcPrim.reserve(numSegments * trisPerSeg);
    md.tris.reserve(numSegments * trisPerSeg * 3);
    md.smooth.reserve(numSegments * trisPerSeg);
  }

  const size_t numSkipped = forEachSegment(
      [&](size_t prim,
          uint64_t v0,
          uint64_t v1,
          const float3 &p0,
          const float3 &p1,
          float L,
          float r0,
          float r1) {
        const float3 axis = (p1 - p0) / L;

        // Right-handed orthonormal frame (u, v, axis).
        const float3 ref = fabsf(axis.x) < 0.9f ? make_float3(1.f, 0.f, 0.f)
                                                : make_float3(0.f, 1.f, 0.f);
        const float3 u = normalize(cross(axis, ref));
        const float3 v = cross(axis, u);

        // Lateral surface: two rings of N vertices, smooth-shaded with
        // analytic normals n = normalize(radial * L + axis * (r0 - r1)),
        // i.e. radial tilted along the axis by the cone slope.
        const uint32_t base = uint32_t(md.verts.size());
        for (uint32_t j = 0; j < N; j++) {
          const float3 dir = ring[j].x * u + ring[j].y * v;
          const float3 n = normalize(dir * L + axis * (r0 - r1));
          md.verts.push_back(p0 + dir * r0);
          md.normals.push_back(n);
          md.srcVertex.push_back(uint32_t(v0));
          md.verts.push_back(p1 + dir * r1);
          md.normals.push_back(n);
          md.srcVertex.push_back(uint32_t(v1));
        }
        for (uint32_t j = 0; j < N; j++) {
          const uint32_t jn = (j + 1) % N;
          const uint32_t a0 = base + 2 * j + 0; // ring0[j]
          const uint32_t a1 = base + 2 * j + 1; // ring1[j]
          const uint32_t b0 = base + 2 * jn + 0; // ring0[j+1]
          const uint32_t b1 = base + 2 * jn + 1; // ring1[j+1]
          md.tris.insert(md.tris.end(), {a0, b0, b1});
          md.smooth.push_back(true);
          md.srcPrim.push_back(uint32_t(prim));
          md.tris.insert(md.tris.end(), {a0, b1, a1});
          md.smooth.push_back(true);
          md.srcPrim.push_back(uint32_t(prim));
        }

        // Flat end-cap disks (fan around a center vertex). Vertices are
        // duplicated so cap shading never bleeds into the lateral surface.
        auto addCap = [&](const float3 &p,
                          float r,
                          uint64_t srcVert,
                          const float3 &capNormal,
                          bool flipWinding) {
          const uint32_t cbase = uint32_t(md.verts.size());
          md.verts.push_back(p);
          md.normals.push_back(capNormal);
          md.srcVertex.push_back(uint32_t(srcVert));
          for (uint32_t j = 0; j < N; j++) {
            const float3 dir = ring[j].x * u + ring[j].y * v;
            md.verts.push_back(p + dir * r);
            md.normals.push_back(capNormal);
            md.srcVertex.push_back(uint32_t(srcVert));
          }
          for (uint32_t j = 0; j < N; j++) {
            const uint32_t jn = (j + 1) % N;
            if (flipWinding)
              md.tris.insert(md.tris.end(), {cbase, cbase + 1 + jn, cbase + 1 + j});
            else
              md.tris.insert(md.tris.end(), {cbase, cbase + 1 + j, cbase + 1 + jn});
            md.smooth.push_back(false);
            md.srcPrim.push_back(uint32_t(prim));
          }
        };

        if (r0 > 0.f && capEnabled(v0, true))
          addCap(p0, r0, v0, -axis, true); // faces -axis
        if (r1 > 0.f && capEnabled(v1, false))
          addCap(p1, r1, v1, axis, false); // faces +axis
      });

  if (numSkipped > 0) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "%s geometry: skipped %zu out-of-range or degenerate segment(s)",
        m_subtype,
        numSkipped);
  }
}

} // namespace anari_cycles
