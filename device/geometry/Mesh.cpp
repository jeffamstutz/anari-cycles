// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Mesh.h"
#include "GeometryAttributes.h"
#include "MotionTrack.h" // detail::keyLocation (deformation key resampling)
// std
#include <algorithm>
#include <cmath>

namespace anari_cycles {

Mesh::Mesh(CyclesGlobalState *s, bool quads, const char *subtype)
    : Geometry(s),
      m_index(this),
      m_vertexPosition(this),
      m_vertexNormal(this),
      m_vertexTangent(this),
      m_positionKeyData(this),
      m_normalKeyData(this),
      m_tangentKeyData(this),
      m_faceVaryingAttr{{{this}, {this}, {this}, {this}, {this}}},
      m_faceVaryingNormal(this),
      m_faceVaryingTangent(this),
      m_creaseIndex(this),
      m_creaseWeight(this),
      m_quads(quads),
      m_subtype(subtype)
{}

Mesh::~Mesh() = default;

void Mesh::commitParameters()
{
  Geometry::commitParameters();
  commitAttributeParameters();

  m_index = getParamObject<Array1D>("primitive.index");
  if (m_index) {
    const anari::DataType t = m_index->elementType();
    const bool valid = m_quads
        ? (t == ANARI_UINT32_VEC4 || t == ANARI_UINT64_VEC4)
        : (t == ANARI_UINT32_VEC3 || t == ANARI_UINT64_VEC3);
    if (!valid) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'primitive.index' on %s geometry must be an array of %s "
          "(got %s) -- ignoring",
          m_subtype,
          m_quads ? "UINT32_VEC4 or UINT64_VEC4" : "UINT32_VEC3 or UINT64_VEC3",
          anari::toString(t));
      m_index = nullptr;
    }
  }

  // KHR_GEOMETRY_*_MOTION_DEFORMATION: each of these vertex parameters may be
  // a nested array of per-key arrays instead of a plain data array. Key
  // *contents* are read in finalize() (array data changes notify change
  // observers, which re-run finalize only); here the parameter is routed to
  // either its nested or its plain slot.
  auto nestedKeysOrNull = [&](const char *name) -> ObjectArray * {
    auto *array = getParamObject<Array1D>(name);
    return array && array->elementType() == ANARI_ARRAY1D
        ? getParamObject<ObjectArray>(name)
        : nullptr;
  };

  m_positionKeyData = nestedKeysOrNull("vertex.position");
  m_vertexPosition =
      m_positionKeyData ? nullptr : validatedVertexPosition(m_subtype).ptr;

  m_normalKeyData = nestedKeysOrNull("vertex.normal");
  m_vertexNormal =
      m_normalKeyData ? nullptr : getParamObject<Array1D>("vertex.normal");
  if (m_vertexNormal && !validNormalArray(this, *m_vertexNormal, "vertex.normal"))
    m_vertexNormal = nullptr;
  m_tangentKeyData = nestedKeysOrNull("vertex.tangent");
  m_vertexTangent =
      m_tangentKeyData ? nullptr : getParamObject<Array1D>("vertex.tangent");
  if (m_vertexTangent
      && !validTangentArray(this, *m_vertexTangent, "vertex.tangent"))
    m_vertexTangent = nullptr;

  m_motionTime = getParam<helium::box1>("time", helium::box1{0.f, 1.f});
  if ((m_positionKeyData || m_normalKeyData || m_tangentKeyData)
      && m_motionTime.upper < m_motionTime.lower) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "invalid 'time' interval [%f, %f] (upper < lower) -- all deformation "
        "keys collapse to the first key",
        m_motionTime.lower,
        m_motionTime.upper);
  }

  for (int c = 0; c < NUM_ATTRIBUTE_CHANNELS; c++) {
    m_faceVaryingAttr[c] = getParamObject<Array1D>(
        std::string("faceVarying.") + CHANNEL_PARAM[c]);
  }
  m_faceVaryingNormal = getParamObject<Array1D>("faceVarying.normal");
  if (m_faceVaryingNormal
      && !validNormalArray(this, *m_faceVaryingNormal, "faceVarying.normal"))
    m_faceVaryingNormal = nullptr;
  m_faceVaryingTangent = getParamObject<Array1D>("faceVarying.tangent");
  if (m_faceVaryingTangent
      && !validTangentArray(this, *m_faceVaryingTangent, "faceVarying.tangent"))
    m_faceVaryingTangent = nullptr;

  // CYCLES_GEOMETRY_SUBDIVISION parameters
  const std::string subdivision = getParamString("subdivision", "none");
  if (subdivision == "none") {
    m_subdivisionType = ccl::Mesh::SUBDIVISION_NONE;
  } else if (subdivision == "linear") {
    m_subdivisionType = ccl::Mesh::SUBDIVISION_LINEAR;
  } else if (subdivision == "catmullClark") {
    m_subdivisionType = ccl::Mesh::SUBDIVISION_CATMULL_CLARK;
  } else {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'subdivision' on %s geometry must be 'none', 'linear' or "
        "'catmullClark' (got '%s') -- disabling subdivision",
        m_subtype,
        subdivision.c_str());
    m_subdivisionType = ccl::Mesh::SUBDIVISION_NONE;
  }
#ifndef WITH_OPENSUBDIV
  if (m_subdivisionType == ccl::Mesh::SUBDIVISION_CATMULL_CLARK) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "this build lacks OpenSubdiv (WITH_CYCLES_OPENSUBDIV=OFF): "
        "'catmullClark' subdivision dices the base mesh linearly");
  }
#endif

  // 2^level caps the per-edge dicing factor; 16 keeps 1<<level well away
  // from overflow and is already far beyond practical memory limits.
  m_subdivisionLevel =
      std::min(std::max(getParam<int>("subdivisionLevel", 12), 0), 16);
  // Tiny rates are meaningful (the level caps the work), but zero/negative
  // rates would make the dicing factor computation degenerate.
  m_subdivisionDicingRate =
      std::max(getParam<float>("subdivisionDicingRate", 1.f), 1e-4f);

  m_creaseIndex = getParamObject<Array1D>("primitive.creaseIndex");
  if (m_creaseIndex) {
    const anari::DataType t = m_creaseIndex->elementType();
    if (t != ANARI_UINT32_VEC2 && t != ANARI_UINT64_VEC2) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'primitive.creaseIndex' on %s geometry must be an array of "
          "UINT32_VEC2 or UINT64_VEC2 (got %s) -- ignoring creases",
          m_subtype,
          anari::toString(t));
      m_creaseIndex = nullptr;
    }
  }
  m_creaseWeight = getParamObject<Array1D>("primitive.creaseWeight");
  if (m_creaseWeight && m_creaseWeight->elementType() != ANARI_FLOAT32) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'primitive.creaseWeight' on %s geometry must be an array of "
        "FLOAT32 (got %s) -- ignoring creases",
        m_subtype,
        anari::toString(m_creaseWeight->elementType()));
    m_creaseWeight = nullptr;
  }
}

void Mesh::finalize()
{
  // KHR_GEOMETRY_*_MOTION_DEFORMATION: (re)read the nested key arrays. All
  // position keys must share one element count (they describe the same
  // vertices at different times); normal/tangent keys follow the usual
  // clamped-index rule of their static counterparts. The first position key
  // becomes the representative static array every non-motion code path
  // (primitive count, attribute sizing, initial sync) reads.
  m_positionKeys.clear();
  m_normalKeys.clear();
  m_tangentKeys.clear();
  if (m_positionKeyData) {
    readVertexKeys(m_positionKeyData.get(),
        "vertex.position",
        true,
        [](const Object *, const Array1D &a, const char *) {
          return a.elementType() == ANARI_FLOAT32_VEC3;
        },
        m_positionKeys);
    m_vertexPosition =
        m_positionKeys.empty() ? nullptr : m_positionKeys.front().get();
  }
  if (m_normalKeyData) {
    readVertexKeys(m_normalKeyData.get(),
        "vertex.normal",
        false,
        validNormalArray,
        m_normalKeys);
    m_vertexNormal = m_normalKeys.empty()
        ? nullptr
        : m_normalKeys[(m_normalKeys.size() - 1) / 2].get();
  }
  if (m_tangentKeyData) {
    readVertexKeys(m_tangentKeyData.get(),
        "vertex.tangent",
        false,
        validTangentArray,
        m_tangentKeys);
    // Cycles has no time-varying tangents: the middle key stands in for the
    // whole track (see the struct comment).
    m_vertexTangent = m_tangentKeys.empty()
        ? nullptr
        : m_tangentKeys[(m_tangentKeys.size() - 1) / 2].get();
  }

  if (!m_vertexPosition) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'vertex.position' on %s geometry",
        m_subtype);
  }

  if (subdivisionEnabled() && m_positionKeys.size() > 1) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "%s geometry: deformation motion keys are ignored while "
        "'subdivision' is active (only the first key renders)",
        m_subtype);
  }

  if (subdivisionEnabled()) {
    if (m_vertexNormal || m_faceVaryingNormal || m_vertexTangent
        || m_faceVaryingTangent) {
      reportMessage(ANARI_SEVERITY_INFO,
          "%s geometry: normals/tangents are ignored while 'subdivision' is "
          "active (the tessellator computes its own)",
          m_subtype);
    }
    if (bool(m_creaseIndex) != bool(m_creaseWeight)) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "%s geometry: 'primitive.creaseIndex' and 'primitive.creaseWeight' "
          "must both be set for creases to apply",
          m_subtype);
    }
  }

  Geometry::finalize();
}

ccl::Geometry *Mesh::createCyclesGeometryNode()
{
  return deviceState()->scene->create_node<ccl::Mesh>();
}

void Mesh::syncCyclesNode(ccl::Geometry *node) const
{
  auto *mesh = (ccl::Mesh *)node;

  // With no positions the mesh syncs empty (rather than early-returning) so a
  // previously synced mesh cannot outlive the removal of 'vertex.position'.
  if (!m_vertexPosition) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "Mesh::syncCyclesNode() detected incomplete %s geometry",
        m_subtype);
    ccl::array<ccl::float3> P;
    mesh->set_verts(P);
    mesh->resize_mesh(0, 0);
    clearSubdivisionState(mesh);
    clearDeformationMotionState(mesh);
    return;
  }

  if (subdivisionEnabled()) {
    clearDeformationMotionState(mesh);
    syncSubdCyclesNode(mesh);
    return;
  }

  clearSubdivisionState(mesh);
  // A geometry that lost its deformation keys must not keep stale motion
  // steps; with keys present the (shutter-dependent) motion state is written
  // by bakeDeformationMotion() during the world rebuild that follows any
  // re-sync.
  if (!hasDeformationMotion())
    clearDeformationMotionState(mesh);
  setVertexPosition(mesh);
  setPrimitiveIndex(mesh);
  setAttributes(mesh);
  setNormals(mesh);
  setTangents(mesh);
}

// Reset a previously subdivided node back to the plain-triangle path. A
// never-subdivided mesh returns immediately, keeping the default path
// overhead-free.
void Mesh::clearSubdivisionState(ccl::Mesh *mesh)
{
  if (mesh->get_subdivision_type() == ccl::Mesh::SUBDIVISION_NONE
      && mesh->get_num_subd_faces() == 0)
    return;

  mesh->set_subdivision_type(ccl::Mesh::SUBDIVISION_NONE);
  mesh->resize_subd_faces(0, 0);
  ccl::array<int> creaseEdges;
  ccl::array<float> creaseWeights;
  mesh->set_subd_creases_edge(creaseEdges);
  mesh->set_subd_creases_weight(creaseWeights);
  // Resets the subd bookkeeping counters (num_subd_added_verts) left behind
  // by a previous tessellation and tags every socket modified so the next
  // scene update rebuilds this geometry from the plain-triangle arrays.
  mesh->clear_non_sockets();
}

void Mesh::syncSubdCyclesNode(ccl::Mesh *mesh) const
{
  // Reset the subd bookkeeping (num_subd_added_verts et al.) from any
  // previous tessellation *before* touching verts or attributes: attribute
  // allocation sizes ATTR_ELEMENT_VERTEX from num-subd-base-verts, which is
  // only correct once the added-vertex count is back to zero. This also tags
  // every socket modified, which forces need_tesselation() true so Cycles
  // re-dices on every re-sync (matching how attribute-only updates must
  // still re-interpolate onto the diced mesh).
  mesh->clear_non_sockets();

  setVertexPosition(mesh);
  // Triangles are produced by the tessellator at scene-update time; clear
  // any previously synced triangle data so it cannot linger.
  mesh->resize_mesh(int(m_vertexPosition->size()), 0);

  // Drop plain-path attributes from a previous non-subdivided sync: the
  // tessellator copies interpolated subd attributes into 'attributes' under
  // the same names and must not collide with stale entries.
  auto &triAttrs = mesh->attributes;
  for (int c = 0; c < NUM_ATTRIBUTE_CHANNELS; c++)
    triAttrs.remove(ustring(CHANNEL_CYCLES_NAME[c]));
  triAttrs.remove(ustring("primitiveId"));
  triAttrs.remove(ATTR_STD_VERTEX_NORMAL);
  triAttrs.remove(ATTR_STD_CORNER_NORMAL);
  triAttrs.remove(ATTR_STD_UV_TANGENT);
  triAttrs.remove(ATTR_STD_UV_TANGENT_SIGN);

  setSubdFaces(mesh);
  setSubdCreases(mesh);
  setSubdAttributes(mesh);

  mesh->set_subdivision_type(m_subdivisionType);
  // Object-space adaptive dicing: rate is a target edge length in object
  // space, so results are camera-independent and instancing-safe (pixel
  // space would need one dicing transform per instance).
  mesh->set_subd_adaptive_space(ccl::Mesh::SUBDIVISION_ADAPTIVE_SPACE_OBJECT);
  mesh->set_subd_max_level(m_subdivisionLevel);
  mesh->set_subd_dicing_rate(m_subdivisionDicingRate);
}

void Mesh::setSubdFaces(ccl::Mesh *mesh) const
{
  const size_t nPrims = numPrims();
  const int arity = m_quads ? 4 : 3;

  const uint32_t *idx32 = nullptr;
  const uint64_t *idx64 = nullptr;
  if (m_index) {
    if (m_index->elementType() == ANARI_UINT64_VEC3
        || m_index->elementType() == ANARI_UINT64_VEC4)
      idx64 = (const uint64_t *)m_index->begin();
    else
      idx32 = (const uint32_t *)m_index->begin();
  }
  auto vertIdx = [&](size_t prim, int c) -> int {
    if (idx64)
      return int(idx64[arity * prim + c]);
    if (idx32)
      return int(idx32[arity * prim + c]);
    return int(arity * prim + c);
  };

  mesh->resize_subd_faces(int(nPrims), int(nPrims * size_t(arity)));

  int *startCorner = mesh->get_subd_start_corner().data();
  int *numCorners = mesh->get_subd_num_corners().data();
  int *shader = mesh->get_subd_shader().data();
  bool *smooth = mesh->get_subd_smooth().data();
  int *ptexOffset = mesh->get_subd_ptex_offset().data();
  int *faceCorners = mesh->get_subd_face_corners().data();

  // Quads map to one ptex patch each, non-quads (triangles) to one per
  // corner (see Mesh::tessellate()).
  const int numPtex = m_quads ? 1 : arity;
  for (size_t i = 0; i < nPrims; i++) {
    startCorner[i] = int(size_t(arity) * i);
    numCorners[i] = arity;
    shader[i] = 0;
    smooth[i] = true;
    ptexOffset[i] = int(size_t(numPtex) * i);
    for (int c = 0; c < arity; c++)
      faceCorners[size_t(arity) * i + c] = vertIdx(i, c);
  }

  mesh->tag_subd_start_corner_modified();
  mesh->tag_subd_num_corners_modified();
  mesh->tag_subd_shader_modified();
  mesh->tag_subd_smooth_modified();
  mesh->tag_subd_ptex_offset_modified();
  mesh->tag_subd_face_corners_modified();
}

void Mesh::setSubdCreases(ccl::Mesh *mesh) const
{
  ccl::array<int> creaseEdges;
  ccl::array<float> creaseWeights;

  if (m_creaseIndex && m_creaseWeight) {
    const size_t n = std::min(m_creaseIndex->size(), m_creaseWeight->size());
    if (m_creaseIndex->size() != m_creaseWeight->size()) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "%s geometry: 'primitive.creaseIndex' (%zu) and "
          "'primitive.creaseWeight' (%zu) sizes differ -- using the first "
          "%zu crease(s)",
          m_subtype,
          m_creaseIndex->size(),
          m_creaseWeight->size(),
          n);
    }

    const uint32_t *idx32 = nullptr;
    const uint64_t *idx64 = nullptr;
    if (m_creaseIndex->elementType() == ANARI_UINT64_VEC2)
      idx64 = (const uint64_t *)m_creaseIndex->begin();
    else
      idx32 = (const uint32_t *)m_creaseIndex->begin();
    const float *weights = m_creaseWeight->beginAs<float>();

    const size_t numVerts = m_vertexPosition->size();
    size_t numSkipped = 0;
    std::vector<int> edges;
    std::vector<float> w;
    edges.reserve(2 * n);
    w.reserve(n);
    for (size_t i = 0; i < n; i++) {
      const uint64_t v0 = idx64 ? idx64[2 * i + 0] : idx32[2 * i + 0];
      const uint64_t v1 = idx64 ? idx64[2 * i + 1] : idx32[2 * i + 1];
      if (v0 >= numVerts || v1 >= numVerts || v0 == v1) {
        numSkipped++;
        continue;
      }
      edges.push_back(int(v0));
      edges.push_back(int(v1));
      // Cycles crease weights live in [0,1]; 1 maps to the maximum
      // OpenSubdiv sharpness (a fully sharp edge).
      w.push_back(std::min(std::max(weights[i], 0.f), 1.f));
    }
    if (numSkipped > 0) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "%s geometry: skipped %zu crease(s) referencing out-of-range or "
          "degenerate vertex pairs",
          m_subtype,
          numSkipped);
    }

    std::copy(edges.begin(), edges.end(), creaseEdges.resize(edges.size()));
    std::copy(w.begin(), w.end(), creaseWeights.resize(w.size()));
  }

  mesh->set_subd_creases_edge(creaseEdges);
  mesh->set_subd_creases_weight(creaseWeights);
}

void Mesh::setSubdAttributes(ccl::Mesh *mesh) const
{
  auto &attrs = mesh->subd_attributes;
  const size_t numVerts = m_vertexPosition->size();
  const size_t nPrims = numPrims();
  const size_t nCorners = nPrims * (m_quads ? 4 : 3);

  // Subd base faces keep the ANARI primitive layout 1:1 (no quad
  // triangulation), so every source rate maps by identity; the tessellator
  // interpolates the values onto the diced triangles.
  auto identity = [](size_t i) { return i; };

  for (int c = 0; c < NUM_ATTRIBUTE_CHANNELS; c++) {
    if (m_faceVaryingAttr[c]) {
      writeAttributeArray(attrs,
          c,
          ATTR_ELEMENT_CORNER,
          nCorners,
          *m_faceVaryingAttr[c],
          identity);
    } else if (m_vertexAttr[c]) {
      writeAttributeArray(
          attrs, c, ATTR_ELEMENT_VERTEX, numVerts, *m_vertexAttr[c], identity);
    } else if (m_primitiveAttr[c]) {
      writeAttributeArray(
          attrs, c, ATTR_ELEMENT_FACE, nPrims, *m_primitiveAttr[c], identity);
    } else if (m_uniformAttr[c]) {
      writeAttributeConstant(attrs, c, *m_uniformAttr[c]);
    } else if (c == CH_COLOR) {
      writeAttributeConstant(attrs, c, DEFAULT_COLOR);
    } else {
      attrs.remove(ustring(CHANNEL_CYCLES_NAME[c]));
    }
  }

  writePrimitiveId(
      attrs, ATTR_ELEMENT_FACE, nPrims, m_primitiveId.get(), identity);
}

box3 Mesh::bounds() const
{
  box3 b = empty_box3();
  if (!m_vertexPosition)
    return b;
  auto extendOver = [&](const Array1D &positions) {
    std::for_each(positions.beginAs<anari_vec::float3>(),
        positions.endAs<anari_vec::float3>(),
        [&](const anari_vec::float3 &v) {
          extend(b, make_float3(v[0], v[1], v[2]));
        });
  };
  if (m_positionKeys.size() > 1) {
    // Deformation keys: cover the whole motion track (keys interpolate
    // linearly, so the union of the key poses bounds every sample time).
    for (const auto &key : m_positionKeys)
      extendOver(*key);
  } else {
    extendOver(*m_vertexPosition);
  }
  return b;
}

size_t Mesh::numPrims() const
{
  if (m_index)
    return m_index->size();
  return m_vertexPosition ? m_vertexPosition->size() / (m_quads ? 4 : 3) : 0;
}

size_t Mesh::fvIndex(size_t corner) const
{
  if (!m_quads)
    return corner;
  const size_t tri = corner / 3;
  const int c = int(corner % 3);
  static const int oddCorner[3] = {0, 2, 3};
  return 4 * (tri / 2) + size_t((tri & 1) ? oddCorner[c] : c);
}

void Mesh::setVertexPosition(ccl::Mesh *mesh) const
{
  ccl::array<ccl::float3> P;
  auto *dst = P.resize(m_vertexPosition->size());
  std::transform(m_vertexPosition->beginAs<anari_vec::float3>(),
      m_vertexPosition->endAs<anari_vec::float3>(),
      dst,
      [](const anari_vec::float3 &v) { return make_float3(v[0], v[1], v[2]); });
  mesh->set_verts(P);
}

void Mesh::setPrimitiveIndex(ccl::Mesh *mesh) const
{
  const size_t nPrims = numPrims();
  const size_t nTris = numTriangles();
  const int arity = m_quads ? 4 : 3;

  const uint32_t *idx32 = nullptr;
  const uint64_t *idx64 = nullptr;
  if (m_index) {
    if (m_index->elementType() == ANARI_UINT64_VEC3
        || m_index->elementType() == ANARI_UINT64_VEC4)
      idx64 = (const uint64_t *)m_index->begin();
    else
      idx32 = (const uint32_t *)m_index->begin();
  }
  auto vertIdx = [&](size_t prim, int c) -> uint32_t {
    if (idx64)
      return uint32_t(idx64[arity * prim + c]);
    if (idx32)
      return idx32[arity * prim + c];
    return uint32_t(arity * prim + c);
  };

  mesh->resize_mesh(m_vertexPosition->size(), nTris);
  auto *triangles = mesh->get_triangles().data();
  auto *shader = mesh->get_shader().data();
  auto *smooth = mesh->get_smooth().data();
  for (size_t i = 0; i < nPrims; i++) {
    if (m_quads) {
      const uint32_t v0 = vertIdx(i, 0);
      const uint32_t v1 = vertIdx(i, 1);
      const uint32_t v2 = vertIdx(i, 2);
      const uint32_t v3 = vertIdx(i, 3);
      const size_t triangle = 2 * i;
      triangles[3 * triangle + 0] = v0;
      triangles[3 * triangle + 1] = v1;
      triangles[3 * triangle + 2] = v2;
      triangles[3 * triangle + 3] = v0;
      triangles[3 * triangle + 4] = v2;
      triangles[3 * triangle + 5] = v3;
    } else {
      triangles[3 * i + 0] = vertIdx(i, 0);
      triangles[3 * i + 1] = vertIdx(i, 1);
      triangles[3 * i + 2] = vertIdx(i, 2);
    }
  }
  for (size_t t = 0; t < nTris; t++) {
    shader[t] = 0;
    smooth[t] = true;
  }
  mesh->tag_triangles_modified();
  mesh->tag_shader_modified();
  mesh->tag_smooth_modified();
}

void Mesh::setAttributes(ccl::Mesh *mesh) const
{
  auto &attrs = mesh->attributes;
  const size_t numVerts = m_vertexPosition->size();
  const size_t nTris = numTriangles();
  const size_t nCorners = 3 * nTris;

  auto fv = [&](size_t corner) { return fvIndex(corner); };
  auto triPrim = [&](size_t tri) { return m_quads ? tri / 2 : tri; };
  auto identity = [](size_t i) { return i; };

  for (int c = 0; c < NUM_ATTRIBUTE_CHANNELS; c++) {
    if (m_faceVaryingAttr[c]) {
      writeAttributeArray(
          attrs, c, ATTR_ELEMENT_CORNER, nCorners, *m_faceVaryingAttr[c], fv);
    } else if (m_vertexAttr[c]) {
      writeAttributeArray(
          attrs, c, ATTR_ELEMENT_VERTEX, numVerts, *m_vertexAttr[c], identity);
    } else if (m_primitiveAttr[c]) {
      writeAttributeArray(
          attrs, c, ATTR_ELEMENT_FACE, nTris, *m_primitiveAttr[c], triPrim);
    } else if (m_uniformAttr[c]) {
      writeAttributeConstant(attrs, c, *m_uniformAttr[c]);
    } else if (c == CH_COLOR) {
      writeAttributeConstant(attrs, c, DEFAULT_COLOR);
    } else {
      attrs.remove(ustring(CHANNEL_CYCLES_NAME[c]));
    }
  }

  writePrimitiveId(attrs, ATTR_ELEMENT_FACE, nTris, m_primitiveId.get(), triPrim);
}

void Mesh::setNormals(ccl::Mesh *mesh) const
{
  const size_t numVerts = m_vertexPosition->size();
  const size_t nCorners = 3 * numTriangles();

  if (m_vertexNormal && m_vertexNormal->size() > 0) {
    Attribute *attr =
        mesh->attributes.add(ATTR_STD_VERTEX_NORMAL, ustring("vertex.normal"));
    packed_normal *dst = attr->data_normal_for_write();
    const auto converted = convertToFloat4(*m_vertexNormal);
    const size_t maxIdx = converted.size() - 1;
    for (size_t i = 0; i < numVerts; i++) {
      const auto &n = converted[std::min(i, maxIdx)];
      dst[i] = packed_normal(make_float3(n[0], n[1], n[2]));
    }
    attr->modified = true;
  } else {
    mesh->attributes.remove(ATTR_STD_VERTEX_NORMAL);
  }

  // faceVarying (split/corner) normals; when present the Cycles kernel
  // prefers them over vertex normals, matching spec precedence.
  if (m_faceVaryingNormal && m_faceVaryingNormal->size() > 0) {
    Attribute *attr = mesh->attributes.add(
        ATTR_STD_CORNER_NORMAL, ustring("faceVarying.normal"));
    packed_normal *dst = attr->data_normal_for_write();
    const auto converted = convertToFloat4(*m_faceVaryingNormal);
    const size_t maxIdx = converted.size() - 1;
    for (size_t k = 0; k < nCorners; k++) {
      const auto &n = converted[std::min(fvIndex(k), maxIdx)];
      dst[k] = packed_normal(make_float3(n[0], n[1], n[2]));
    }
    attr->modified = true;
  } else {
    mesh->attributes.remove(ATTR_STD_CORNER_NORMAL);
  }
}

void Mesh::setTangents(ccl::Mesh *mesh) const
{
  // faceVarying.tangent > vertex.tangent; uploaded as the standard Cycles UV
  // tangent (+ handedness sign) that tangent-space normal mapping consumes
  // (see the NormalMapNode setup in material/Material.cpp). Both are per-corner
  // attributes in Cycles, so vertex tangents replicate through the triangle
  // index.
  const Array1D *src =
      m_faceVaryingTangent ? m_faceVaryingTangent.get() : m_vertexTangent.get();
  if (!src || src->size() == 0) {
    mesh->attributes.remove(ATTR_STD_UV_TANGENT);
    mesh->attributes.remove(ATTR_STD_UV_TANGENT_SIGN);
    return;
  }

  const bool faceVarying = m_faceVaryingTangent;
  const size_t nCorners = 3 * numTriangles();
  const int *triangles = mesh->get_triangles().data();

  const auto converted = convertToFloat4(*src);
  const size_t maxIdx = converted.size() - 1;

  Attribute *attrT = mesh->attributes.add(ATTR_STD_UV_TANGENT);
  Attribute *attrS = mesh->attributes.add(ATTR_STD_UV_TANGENT_SIGN);
  float3 *dstT = attrT->data_float3_for_write();
  float *dstS = attrS->data_float_for_write();
  for (size_t k = 0; k < nCorners; k++) {
    const size_t i =
        faceVarying ? fvIndex(k) : size_t(std::max(triangles[k], 0));
    const auto &t = converted[std::min(i, maxIdx)];
    dstT[k] = make_float3(t[0], t[1], t[2]);
    dstS[k] = t[3] < 0.f ? -1.f : 1.f;
  }
  attrT->modified = true;
  attrS->modified = true;
}

// KHR_GEOMETRY_TRIANGLE/QUAD_MOTION_DEFORMATION //////////////////////////////

bool Mesh::hasDeformationMotion() const
{
  return m_positionKeys.size() > 1 && !subdivisionEnabled();
}

template <typename ValidFn>
void Mesh::readVertexKeys(const ObjectArray *data,
    const char *param,
    bool requireEqualSizes,
    ValidFn &&valid,
    KeyVector &out)
{
  if (!data)
    return;
  size_t numSkipped = 0;
  size_t keySize = 0;
  for (auto **h = data->handlesBegin(); h != data->handlesEnd(); h++) {
    auto *arr = (*h)->type() == ANARI_ARRAY1D ? (Array1D *)*h : nullptr;
    bool ok = arr && valid(this, *arr, param);
    if (ok && requireEqualSizes) {
      if (out.empty())
        keySize = arr->size();
      else
        ok = arr->size() == keySize;
    }
    if (!ok) {
      numSkipped++;
      continue;
    }
    out.emplace_back(this, arr);
  }
  if (numSkipped > 0) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "%s geometry: skipped %zu '%s' deformation key array(s) with a "
        "mismatched element type or size",
        m_subtype,
        numSkipped,
        param);
  }
}

void Mesh::samplePositionKeys(float t, ccl::float3 *dst) const
{
  const size_t numVerts = m_vertexPosition->size();
  size_t j;
  float f;
  detail::keyLocation(m_motionTime, m_positionKeys.size(), t, j, f);
  const auto *a = m_positionKeys[j]->beginAs<anari_vec::float3>();
  if (f == 0.f) {
    for (size_t i = 0; i < numVerts; i++)
      dst[i] = make_float3(a[i][0], a[i][1], a[i][2]);
  } else {
    const auto *b = m_positionKeys[j + 1]->beginAs<anari_vec::float3>();
    const float g = 1.f - f;
    for (size_t i = 0; i < numVerts; i++) {
      dst[i] = make_float3(g * a[i][0] + f * b[i][0],
          g * a[i][1] + f * b[i][1],
          g * a[i][2] + f * b[i][2]);
    }
  }
}

void Mesh::sampleNormalKeys(const ConvertedKeys &keys,
    float t,
    ccl::packed_normal *dst,
    size_t count) const
{
  const float3 fallback = make_float3(0.f, 0.f, 1.f);
  size_t j;
  float f;
  detail::keyLocation(m_motionTime, keys.size(), t, j, f);
  const auto &a = keys[j];
  static const std::vector<anari_vec::float4> emptyKey;
  const auto &b = f != 0.f ? keys[j + 1] : emptyKey;
  for (size_t i = 0; i < count; i++) {
    float3 n = fallback;
    if (!a.empty()) {
      const auto &na = a[std::min(i, a.size() - 1)];
      n = make_float3(na[0], na[1], na[2]);
      if (!b.empty()) {
        const auto &nb = b[std::min(i, b.size() - 1)];
        n = (1.f - f) * n + f * make_float3(nb[0], nb[1], nb[2]);
      }
      const float l = len(n);
      n = l > 0.f ? n / l : fallback;
    }
    dst[i] = packed_normal(n);
  }
}

// Reset a node that previously carried baked deformation steps back to the
// static path. A never-deforming mesh returns immediately, keeping the
// common case overhead-free.
void Mesh::clearDeformationMotionState(ccl::Mesh *mesh)
{
  const bool haveAttrs =
      mesh->attributes.find(ATTR_STD_MOTION_VERTEX_POSITION) != nullptr
      || mesh->attributes.find(ATTR_STD_MOTION_VERTEX_NORMAL) != nullptr;
  if (!haveAttrs && mesh->get_motion_steps() == 0
      && !mesh->get_use_motion_blur())
    return;
  mesh->attributes.remove(ATTR_STD_MOTION_VERTEX_POSITION);
  mesh->attributes.remove(ATTR_STD_MOTION_VERTEX_NORMAL);
  mesh->set_motion_steps(0);
  mesh->set_use_motion_blur(false);
}

bool Mesh::bakeDeformationMotion(
    ccl::Geometry *node, const helium::box1 &shutter) const
{
  auto *mesh = (ccl::Mesh *)node;
  if (!hasDeformationMotion() || !m_vertexPosition) { // callers guard this
    clearDeformationMotionState(mesh);
    return false;
  }

  const size_t numVerts = m_vertexPosition->size();
  const size_t numKeys = m_positionKeys.size();
  const float extent = shutter.upper - shutter.lower;

  // Step count N (odd -- Cycles stores the center step in the base verts,
  // see motion_triangle.h): when the shutter spans exactly the 'time'
  // interval the steps land on the source keys (odd key counts directly,
  // even counts via 2M-1, which adds the segment midpoints and is exact for
  // linearly interpolated keys); a shutter that is a strict sub-interval is
  // refined 4x, mirroring bakeMotionOnShutter() (see the resampling contract
  // in MotionTrack.h).
  size_t n = 1;
  if (extent > 0.f) {
    const bool aligned = shutter.lower == m_motionTime.lower
        && shutter.upper == m_motionTime.upper;
    n = aligned ? (numKeys % 2 ? numKeys : 2 * numKeys - 1)
                : (numKeys - 1) * 4 + 1;
    n = std::min(n, size_t(ccl::Object::MAX_MOTION_STEPS)); // 129 (odd)
    n = std::max(n, size_t(3));
    if (n % 2 == 0)
      n--; // defensive; the formulas above already produce odd counts
  }

  // Sample every step up front so a track that does not actually move across
  // the shutter (or a degenerate shutter, n == 1) collapses to a static pose
  // and keeps all motion machinery off.
  std::vector<ccl::float3> steps(n * numVerts);
  bool allEqual = true;
  for (size_t s = 0; s < n; s++) {
    const float t = n > 1 ? shutter.lower + extent * float(s) / float(n - 1)
                          : shutter.lower;
    samplePositionKeys(t, steps.data() + s * numVerts);
    for (size_t i = 0; s > 0 && allEqual && i < numVerts; i++)
      allEqual = steps[s * numVerts + i] == steps[i];
  }

  const bool motion = n > 1 && !allEqual;
  const size_t center = motion ? (n - 1) / 2 : 0;

  // Base verts hold the center step: the shutter-midpoint pose (for the
  // static collapse every sampled step is the same pose, and a degenerate
  // shutter's single sample at s0 == its midpoint).
  {
    ccl::array<ccl::float3> P;
    auto *dst = P.resize(numVerts);
    std::copy_n(steps.data() + center * numVerts, numVerts, dst);
    mesh->set_verts(P);
  }

  if (!motion) {
    clearDeformationMotionState(mesh);
  } else {
    mesh->set_motion_steps(uint(n));
    mesh->set_use_motion_blur(true);
    // Remove-then-add so the attribute is (re)allocated for the current step
    // and vertex counts (add() reuses an existing allocation as-is).
    mesh->attributes.remove(ATTR_STD_MOTION_VERTEX_POSITION);
    Attribute *attr = mesh->attributes.add(ATTR_STD_MOTION_VERTEX_POSITION);
    ccl::float3 *dst = attr->data_float3_for_write();
    for (size_t s = 0; s < n; s++) {
      if (s == center)
        continue;
      std::copy_n(steps.data() + s * numVerts, numVerts, dst);
      dst += numVerts;
    }
    attr->modified = true;
  }

  // Nested vertex normals: the center pose goes into the regular normal
  // attribute (the kernel's center step reads it), the other steps into the
  // motion normal attribute. Keys are converted to float4 once up front (not
  // per sampled step).
  ConvertedKeys normalKeys;
  normalKeys.reserve(m_normalKeys.size());
  for (const auto &key : m_normalKeys)
    normalKeys.push_back(convertToFloat4(*key));
  if (!normalKeys.empty()) {
    Attribute *attrN =
        mesh->attributes.add(ATTR_STD_VERTEX_NORMAL, ustring("vertex.normal"));
    const float tCenter = n > 1
        ? shutter.lower + extent * float(center) / float(n - 1)
        : shutter.lower;
    sampleNormalKeys(normalKeys, tCenter, attrN->data_normal_for_write(), numVerts);
    attrN->modified = true;
  }
  if (motion && normalKeys.size() > 1) {
    mesh->attributes.remove(ATTR_STD_MOTION_VERTEX_NORMAL);
    Attribute *attrMN = mesh->attributes.add(ATTR_STD_MOTION_VERTEX_NORMAL);
    packed_normal *dst = attrMN->data_normal_for_write();
    for (size_t s = 0; s < n; s++) {
      if (s == center)
        continue;
      const float t = shutter.lower + extent * float(s) / float(n - 1);
      sampleNormalKeys(normalKeys, t, dst, numVerts);
      dst += numVerts;
    }
    attrMN->modified = true;
  } else {
    mesh->attributes.remove(ATTR_STD_MOTION_VERTEX_NORMAL);
  }

  // The bake changes the mesh's BVH primitive layout whenever the motion
  // step count flips or changes (static triangles vs. motion triangles with
  // N steps), so a refit is not enough -- request a BVH rebuild like
  // Surface::finalize() does after a re-sync. Without it the sharp pose
  // after a blur (or vice versa) traces against a stale motion BVH and
  // renders nothing.
  mesh->tag_update(deviceState()->scene, true);

  return motion;
}

} // namespace anari_cycles
