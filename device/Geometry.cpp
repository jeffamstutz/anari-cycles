// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Geometry.h"
// cycles
#include "scene/hair.h"
#include "scene/mesh.h"
#include "scene/pointcloud.h"
// std
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace anari_cycles {

// Helper types/functions /////////////////////////////////////////////////////

template <int T>
struct convert_toFloat4
{
  using base_type = typename anari::ANARITypeProperties<T>::base_type;
  const int nc = anari::ANARITypeProperties<T>::components;
  anari_vec::float4 operator()(const void *src, size_t offset)
  {
    anari_vec::float4 retval = {0.f, 0.f, 0.f, 1.f};
    if constexpr (!anari::isObject(T) && T != ANARI_UNKNOWN)
      anari::ANARITypeProperties<T>::toFloat4(
          &retval[0], (const base_type *)src + nc * offset);
    return retval;
  }
};

// ANARI geometries expose five general attribute channels ('color' and
// 'attribute0'..'attribute3'), each of which may be fed from several source
// rates. Per the ANARI spec the most specific rate wins:
//
//   faceVarying.X > vertex.X > primitive.X > uniform X (plain FLOAT32_VEC4)
//
// The winning source is uploaded to Cycles under one canonical name per
// channel; material shader graphs look attributes up by exactly these names
// (see Material::makeGraph()). The names keep their historical "vertex."
// prefix even though they may hold data at any source rate.
enum AttributeChannel
{
  CH_COLOR = 0,
  CH_ATTRIBUTE0,
  CH_ATTRIBUTE1,
  CH_ATTRIBUTE2,
  CH_ATTRIBUTE3,
};

static const char *CHANNEL_PARAM[Geometry::NUM_ATTRIBUTE_CHANNELS] = {
    "color", "attribute0", "attribute1", "attribute2", "attribute3"};

static const char *CHANNEL_CYCLES_NAME[Geometry::NUM_ATTRIBUTE_CHANNELS] = {
    "vertex.color",
    "vertex.attribute0",
    "vertex.attribute1",
    "vertex.attribute2",
    "vertex.attribute3"};

// Cycles' AttributeNode outputs (0,0,0) for absent attributes, but the ANARI
// default for the 'color' attribute is opaque white — geometries without any
// color source upload this constant instead of leaving the attribute absent.
static constexpr anari_vec::float4 DEFAULT_COLOR = {1.f, 1.f, 1.f, 1.f};

// Convert an ANARI array to float4 once per source element (a source element
// commonly feeds many Cycles elements — corners, tessellated vertices — and
// the per-type dispatch is the expensive part of the conversion).
static std::vector<anari_vec::float4> convertToFloat4(const Array1D &array)
{
  std::vector<anari_vec::float4> out(array.size());
  const void *src = array.data();
  const anari::DataType type = array.elementType();
  for (size_t i = 0; i < out.size(); i++) {
    out[i] = anari::anariTypeInvoke<anari_vec::float4, convert_toFloat4>(
        type, src, i);
  }
  return out;
}

// Write one ANARI attribute array into 'attrs' under its channel's canonical
// name. 'count' is the Cycles element count for 'element'; srcIndex(i) maps
// Cycles element i to an index into 'array' (clamped to the array bounds).
// The color channel is stored as a Cycles color (float3, matching what
// material graphs consume); other channels keep all four components.
template <typename IndexFn>
static void writeAttributeArray(ccl::AttributeSet &attrs,
    int channel,
    AttributeElement element,
    size_t count,
    const Array1D &array,
    IndexFn &&srcIndex)
{
  const ustring name(CHANNEL_CYCLES_NAME[channel]);
  if (array.size() == 0) {
    attrs.remove(name);
    return;
  }

  const auto converted = convertToFloat4(array);
  const size_t maxIdx = converted.size() - 1;

  if (channel == CH_COLOR) {
    Attribute *attr = attrs.add(name, ccl::TypeColor, element);
    float3 *dst = attr->data_float3_for_write();
    for (size_t i = 0; i < count; i++) {
      const auto &c = converted[std::min<size_t>(srcIndex(i), maxIdx)];
      dst[i] = make_float3(c[0], c[1], c[2]);
    }
    attr->modified = true;
  } else {
    Attribute *attr = attrs.add(name, ccl::TypeFloat4, element);
    float4 *dst = attr->data_float4_for_write();
    for (size_t i = 0; i < count; i++) {
      const auto &c = converted[std::min<size_t>(srcIndex(i), maxIdx)];
      dst[i] = make_float4(c[0], c[1], c[2], c[3]);
    }
    attr->modified = true;
  }
}

// Write a constant (uniform) attribute channel value as a per-geometry
// (ATTR_ELEMENT_MESH) attribute — a single value the kernel reads for every
// shading point on this geometry.
static void writeAttributeConstant(
    ccl::AttributeSet &attrs, int channel, const anari_vec::float4 &v)
{
  const ustring name(CHANNEL_CYCLES_NAME[channel]);
  if (channel == CH_COLOR) {
    Attribute *attr = attrs.add(name, ccl::TypeColor, ATTR_ELEMENT_MESH);
    attr->data_float3_for_write()[0] = make_float3(v[0], v[1], v[2]);
    attr->modified = true;
  } else {
    Attribute *attr = attrs.add(name, ccl::TypeFloat4, ATTR_ELEMENT_MESH);
    attr->data_float4_for_write()[0] = make_float4(v[0], v[1], v[2], v[3]);
    attr->modified = true;
  }
}

// The ANARI 'primitiveId' attribute: primitive.id[prim] when the parameter is
// set, the primitive index itself otherwise. Cycles attributes are
// float-typed, so ids are exact up to 2^24.
template <typename IndexFn>
static void writePrimitiveId(ccl::AttributeSet &attrs,
    AttributeElement element,
    size_t count,
    const Array1D *ids,
    IndexFn &&primIndex)
{
  const uint32_t *id32 = nullptr;
  const uint64_t *id64 = nullptr;
  size_t maxIdx = 0;
  if (ids && ids->size() > 0) {
    maxIdx = ids->size() - 1;
    if (ids->elementType() == ANARI_UINT64)
      id64 = ids->beginAs<uint64_t>();
    else
      id32 = ids->beginAs<uint32_t>();
  }

  Attribute *attr = attrs.add(ustring("primitiveId"), ccl::TypeFloat, element);
  float *dst = attr->data_float_for_write();
  for (size_t i = 0; i < count; i++) {
    const size_t prim = primIndex(i);
    uint64_t id = prim;
    if (id64)
      id = id64[std::min(prim, maxIdx)];
    else if (id32)
      id = id32[std::min(prim, maxIdx)];
    dst[i] = float(id);
  }
  attr->modified = true;
}

// 'vertex.normal'/'faceVarying.normal' arrays must be FLOAT32_VEC3 or
// FIXED16_VEC3 per spec; reject anything else with a warning.
static bool validNormalArray(
    const Object *obj, const Array1D &array, const char *param)
{
  const anari::DataType t = array.elementType();
  if (t == ANARI_FLOAT32_VEC3 || t == ANARI_FIXED16_VEC3)
    return true;
  obj->reportMessage(ANARI_SEVERITY_WARNING,
      "'%s' must be an array of FLOAT32_VEC3 or FIXED16_VEC3 (got %s) "
      "-- ignoring",
      param,
      anari::toString(t));
  return false;
}

// 'vertex.tangent'/'faceVarying.tangent' additionally allow VEC4 variants
// (the 4th component is the bitangent handedness sign).
static bool validTangentArray(
    const Object *obj, const Array1D &array, const char *param)
{
  const anari::DataType t = array.elementType();
  if (t == ANARI_FLOAT32_VEC3 || t == ANARI_FIXED16_VEC3
      || t == ANARI_FLOAT32_VEC4 || t == ANARI_FIXED16_VEC4)
    return true;
  obj->reportMessage(ANARI_SEVERITY_WARNING,
      "'%s' must be an array of FLOAT32/FIXED16 VEC3 or VEC4 (got %s) "
      "-- ignoring",
      param,
      anari::toString(t));
  return false;
}

// Mesh definitions (triangle + quad) /////////////////////////////////////////

// Both subtypes become a Cycles triangle mesh; quads split into two triangles
// each: (v0,v1,v2) and (v0,v2,v3). Per-primitive attributes replicate across
// both split triangles, faceVarying values map through the split corners.
struct Mesh : public Geometry
{
  Mesh(CyclesGlobalState *s, bool quads, const char *subtype);
  ~Mesh() override;

  void commitParameters() override;
  void finalize() override;

  ccl::Geometry *createCyclesGeometryNode() override;
  void syncCyclesNode(ccl::Geometry *node) const override;

  box3 bounds() const override;

 private:
  size_t numPrims() const; // ANARI primitives (triangles or quads)
  size_t numTriangles() const
  {
    return numPrims() * (m_quads ? 2 : 1);
  }
  // Index into a faceVarying array for corner 'corner' of the triangulated
  // mesh (3 corners per triangle; quads have 4 faceVarying values per
  // primitive that map to split-triangle corners {0,1,2} and {0,2,3}).
  size_t fvIndex(size_t corner) const;

  void setVertexPosition(ccl::Mesh *mesh) const;
  void setPrimitiveIndex(ccl::Mesh *mesh) const;
  void setAttributes(ccl::Mesh *mesh) const;
  void setNormals(ccl::Mesh *mesh) const;
  void setTangents(ccl::Mesh *mesh) const;

  helium::ChangeObserverPtr<Array1D> m_index;
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  helium::IntrusivePtr<Array1D> m_vertexNormal;
  helium::IntrusivePtr<Array1D> m_vertexTangent;
  std::array<helium::IntrusivePtr<Array1D>, NUM_ATTRIBUTE_CHANNELS>
      m_faceVaryingAttr;
  helium::IntrusivePtr<Array1D> m_faceVaryingNormal;
  helium::IntrusivePtr<Array1D> m_faceVaryingTangent;
  bool m_quads{false};
  const char *m_subtype{"triangle"};
};

Mesh::Mesh(CyclesGlobalState *s, bool quads, const char *subtype)
    : Geometry(s),
      m_index(this),
      m_vertexPosition(this),
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

  m_vertexPosition = validatedVertexPosition(m_subtype).ptr;

  m_vertexNormal = getParamObject<Array1D>("vertex.normal");
  if (m_vertexNormal && !validNormalArray(this, *m_vertexNormal, "vertex.normal"))
    m_vertexNormal = nullptr;
  m_vertexTangent = getParamObject<Array1D>("vertex.tangent");
  if (m_vertexTangent
      && !validTangentArray(this, *m_vertexTangent, "vertex.tangent"))
    m_vertexTangent = nullptr;

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
}

void Mesh::finalize()
{
  if (!m_vertexPosition) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'vertex.position' on %s geometry",
        m_subtype);
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
    return;
  }

  setVertexPosition(mesh);
  setPrimitiveIndex(mesh);
  setAttributes(mesh);
  setNormals(mesh);
  setTangents(mesh);
}

box3 Mesh::bounds() const
{
  box3 b = empty_box3();
  if (!m_vertexPosition)
    return b;
  std::for_each(m_vertexPosition->beginAs<anari_vec::float3>(),
      m_vertexPosition->endAs<anari_vec::float3>(),
      [&](const anari_vec::float3 &v) {
        extend(b, make_float3(v[0], v[1], v[2]));
      });
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
      idx64 = (const uint64_t *)m_index->data();
    else
      idx32 = (const uint32_t *)m_index->data();
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

  writePrimitiveId(attrs, ATTR_ELEMENT_FACE, nTris, m_primitiveId.ptr, triPrim);
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
  // (see the NormalMapNode setup in Material.cpp). Both are per-corner
  // attributes in Cycles, so vertex tangents replicate through the triangle
  // index.
  const Array1D *src =
      m_faceVaryingTangent ? m_faceVaryingTangent.ptr : m_vertexTangent.ptr;
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

// Sphere definitions /////////////////////////////////////////////////////////

struct Sphere : public Geometry
{
  Sphere(CyclesGlobalState *s);
  ~Sphere() override;

  void commitParameters() override;
  void finalize() override;

  ccl::Geometry *createCyclesGeometryNode() override;
  void syncCyclesNode(ccl::Geometry *node) const override;

  box3 bounds() const override;

 private:
  void setSpheres(ccl::PointCloud *pc) const;
  void setAttributes(ccl::PointCloud *pc) const;

  helium::ChangeObserverPtr<Array1D> m_index;
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  helium::IntrusivePtr<Array1D> m_vertexRadius;
  float m_radius{1.f};
};

Sphere::Sphere(CyclesGlobalState *s)
    : Geometry(s), m_index(this), m_vertexPosition(this)
{}

Sphere::~Sphere() = default;

void Sphere::commitParameters()
{
  Geometry::commitParameters();
  commitAttributeParameters();

  m_index = getParamObject<Array1D>("primitive.index");
  if (m_index) {
    const anari::DataType t = m_index->elementType();
    if (t != ANARI_UINT32 && t != ANARI_UINT64) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'primitive.index' on sphere geometry must be an array of UINT32 "
          "or UINT64 (got %s) -- ignoring",
          anari::toString(t));
      m_index = nullptr;
    }
  }
  m_vertexPosition = validatedVertexPosition("sphere").ptr;
  m_vertexRadius = getParamObject<Array1D>("vertex.radius");
  m_radius = getParam<float>("radius", 1.f);
}

void Sphere::finalize()
{
  if (!m_vertexPosition) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'vertex.position' on sphere geometry");
  }

  Geometry::finalize();
}

ccl::Geometry *Sphere::createCyclesGeometryNode()
{
  return deviceState()->scene->create_node<ccl::PointCloud>();
}

void Sphere::syncCyclesNode(ccl::Geometry *node) const
{
  if (!m_vertexPosition) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "Spheres::syncCyclesNode() detected incomplete geometry");
  }

  auto *pc = (ccl::PointCloud *)node;
  setSpheres(pc);
  setAttributes(pc);
}

box3 Sphere::bounds() const
{
  box3 b = empty_box3();
  if (!m_vertexPosition)
    return b;
  std::for_each(m_vertexPosition->beginAs<anari_vec::float3>(),
      m_vertexPosition->endAs<anari_vec::float3>(),
      [&](const anari_vec::float3 &v) {
        extend(b, make_float3(v[0], v[1], v[2]));
      });
  return b;
}

void Sphere::setSpheres(ccl::PointCloud *pc) const
{
  ccl::array<ccl::float3> points;
  ccl::array<float> radius;
  ccl::array<int> shader;

  const size_t numSpheres = m_vertexPosition
      ? (m_index ? m_index->size() : m_vertexPosition->size())
      : 0;

  auto *dstPoint = (ccl::float3 *)points.resize(numSpheres);
  auto *dstRadius = (float *)radius.resize(numSpheres);
  auto *dstShader = (int *)shader.resize(numSpheres);

  const auto *srcPoint = m_vertexPosition
      ? m_vertexPosition->beginAs<anari_vec::float3>()
      : nullptr;
  const float *srcRadius = nullptr;
  if (m_vertexRadius)
    srcRadius = m_vertexRadius->beginAs<float>();

  const uint32_t *idx32 = nullptr;
  const uint64_t *idx64 = nullptr;
  if (m_index) {
    if (m_index->elementType() == ANARI_UINT64)
      idx64 = m_index->beginAs<uint64_t>();
    else
      idx32 = m_index->beginAs<uint32_t>();
  }

  for (size_t i = 0; i < numSpheres; i++) {
    const size_t idx = idx64 ? size_t(idx64[i]) : (idx32 ? size_t(idx32[i]) : i);
    const auto &pt = srcPoint[idx];
    dstPoint[i] = make_float3(pt[0], pt[1], pt[2]);
    dstRadius[i] = srcRadius ? srcRadius[idx] : m_radius;
    dstShader[i] = 0;
  }

  pc->set_points(points);
  pc->set_radius(radius);
  pc->set_shader(shader);

  // Attributes added on a previous sync keep their old element count; resize
  // them to the new point count before setAttributes() writes them.
  pc->attributes.resize();
}

void Sphere::setAttributes(ccl::PointCloud *pc) const
{
  auto &attrs = pc->attributes;
  const size_t numSpheres = m_vertexPosition
      ? (m_index ? m_index->size() : m_vertexPosition->size())
      : 0;

  const uint32_t *idx32 = nullptr;
  const uint64_t *idx64 = nullptr;
  if (m_index) {
    if (m_index->elementType() == ANARI_UINT64)
      idx64 = m_index->beginAs<uint64_t>();
    else
      idx32 = m_index->beginAs<uint32_t>();
  }

  // Each Cycles point is one ANARI primitive, so both vertex-rate (indexed
  // through 'primitive.index') and primitive-rate (direct) attributes land on
  // the per-point element.
  auto vertexOf = [&](size_t i) -> size_t {
    return idx64 ? size_t(idx64[i]) : (idx32 ? size_t(idx32[i]) : i);
  };
  auto identity = [](size_t i) { return i; };

  for (int c = 0; c < NUM_ATTRIBUTE_CHANNELS; c++) {
    if (m_vertexAttr[c]) {
      writeAttributeArray(
          attrs, c, ATTR_ELEMENT_VERTEX, numSpheres, *m_vertexAttr[c], vertexOf);
    } else if (m_primitiveAttr[c]) {
      writeAttributeArray(attrs,
          c,
          ATTR_ELEMENT_VERTEX,
          numSpheres,
          *m_primitiveAttr[c],
          identity);
    } else if (m_uniformAttr[c]) {
      writeAttributeConstant(attrs, c, *m_uniformAttr[c]);
    } else if (c == CH_COLOR) {
      writeAttributeConstant(attrs, c, DEFAULT_COLOR);
    } else {
      attrs.remove(ustring(CHANNEL_CYCLES_NAME[c]));
    }
  }

  writePrimitiveId(
      attrs, ATTR_ELEMENT_VERTEX, numSpheres, m_primitiveId.ptr, identity);
}

// Curve definitions //////////////////////////////////////////////////////////

struct Curve : public Geometry
{
  Curve(CyclesGlobalState *s);
  ~Curve() override;

  void commitParameters() override;
  void finalize() override;

  ccl::Geometry *createCyclesGeometryNode() override;
  void syncCyclesNode(ccl::Geometry *node) const override;

  box3 bounds() const override;

 private:
  // Convert ANARI 2-vertex segments to Cycles curves, merging runs of
  // consecutive segments that share a vertex into multi-key curves.
  // keyVertex[k] is the source ANARI vertex of Cycles key k, firstKey[c] the
  // first key of Cycles curve c, curvePrim[c] the ANARI segment (primitive)
  // index curve c starts at. Merging is disabled whenever per-primitive
  // attribute sources are present (a merged curve stores only one
  // ATTR_ELEMENT_CURVE value); the implicit 'primitiveId' of a merged curve
  // is therefore approximated by its first segment's index.
  void computeCurveLayout(std::vector<int> &firstKey,
      std::vector<uint32_t> &keyVertex,
      std::vector<uint32_t> &curvePrim) const;
  void setCurves(ccl::Hair *hair,
      const std::vector<int> &firstKey,
      const std::vector<uint32_t> &keyVertex) const;
  void setAttributes(ccl::Hair *hair,
      const std::vector<uint32_t> &keyVertex,
      const std::vector<uint32_t> &curvePrim) const;

  helium::ChangeObserverPtr<Array1D> m_index;
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  helium::IntrusivePtr<Array1D> m_vertexRadius;
  float m_radius{1.f};
};

Curve::Curve(CyclesGlobalState *s)
    : Geometry(s), m_index(this), m_vertexPosition(this)
{}

Curve::~Curve() = default;

void Curve::commitParameters()
{
  Geometry::commitParameters();
  commitAttributeParameters();

  m_index = getParamObject<Array1D>("primitive.index");
  if (m_index) {
    const anari::DataType t = m_index->elementType();
    if (t != ANARI_UINT32 && t != ANARI_UINT64) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'primitive.index' on curve geometry must be an array of UINT32 "
          "or UINT64 (got %s) -- ignoring",
          anari::toString(t));
      m_index = nullptr;
    }
  }
  m_vertexPosition = validatedVertexPosition("curve").ptr;
  m_vertexRadius = getParamObject<Array1D>("vertex.radius");
  m_radius = getParam<float>("radius", 1.f);
}

void Curve::finalize()
{
  if (!m_vertexPosition) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'vertex.position' on curve geometry");
  }

  Geometry::finalize();
}

ccl::Geometry *Curve::createCyclesGeometryNode()
{
  auto *hair = deviceState()->scene->create_node<ccl::Hair>();
  hair->curve_shape = ccl::CURVE_THICK_LINEAR;
  return hair;
}

void Curve::syncCyclesNode(ccl::Geometry *node) const
{
  auto *hair = (ccl::Hair *)node;

  if (!m_vertexPosition) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "Curve::syncCyclesNode() detected incomplete geometry");
    return;
  }

  std::vector<int> firstKey;
  std::vector<uint32_t> keyVertex;
  std::vector<uint32_t> curvePrim;
  computeCurveLayout(firstKey, keyVertex, curvePrim);

  setCurves(hair, firstKey, keyVertex);
  setAttributes(hair, keyVertex, curvePrim);
}

box3 Curve::bounds() const
{
  box3 b = empty_box3();
  if (!m_vertexPosition)
    return b;

  // Only vertices referenced by segments contribute (unused vertices must not
  // inflate the bounds); computeCurveLayout() defines which those are.
  std::vector<int> firstKey;
  std::vector<uint32_t> keyVertex;
  std::vector<uint32_t> curvePrim;
  computeCurveLayout(firstKey, keyVertex, curvePrim);

  const float *srcRadius =
      m_vertexRadius ? m_vertexRadius->beginAs<float>() : nullptr;
  const auto *srcPoint = m_vertexPosition->beginAs<anari_vec::float3>();
  for (uint32_t vi : keyVertex) {
    const auto &v = srcPoint[vi];
    const float r = srcRadius ? srcRadius[vi] : m_radius;
    extend(b, make_float3(v[0] - r, v[1] - r, v[2] - r));
    extend(b, make_float3(v[0] + r, v[1] + r, v[2] + r));
  }
  return b;
}

void Curve::computeCurveLayout(std::vector<int> &firstKey,
    std::vector<uint32_t> &keyVertex,
    std::vector<uint32_t> &curvePrim) const
{
  const size_t numVerts = m_vertexPosition->size();
  const size_t nSeg = m_index ? m_index->size() : numVerts / 2;

  const uint32_t *idx32 = nullptr;
  const uint64_t *idx64 = nullptr;
  if (m_index) {
    if (m_index->elementType() == ANARI_UINT64)
      idx64 = m_index->beginAs<uint64_t>();
    else
      idx32 = m_index->beginAs<uint32_t>();
  }

  firstKey.reserve(nSeg);
  keyVertex.reserve(nSeg * 2);
  curvePrim.reserve(nSeg);

  // Runs of consecutive segments sharing a vertex ((a,a+1),(a+1,a+2),...)
  // merge into one multi-key Cycles curve. Thick-linear curves have spherical
  // end caps, so the union of per-segment 2-key curves is geometrically
  // identical; merging just shares the interior keys. Per-primitive attribute
  // values only exist per Cycles curve, so merging is disabled when any are
  // present (see computeCurveLayout() docs).
  const bool merge = !hasPerPrimitiveAttributes();

  bool chainActive = false;
  uint64_t prevV0 = 0;
  size_t numSkipped = 0;
  for (size_t i = 0; i < nSeg; i++) {
    const uint64_t v0 = idx64 ? idx64[i] : (idx32 ? idx32[i] : i);
    if (numVerts < 2 || v0 > numVerts - 2) { // overflow-safe v0 + 1 >= numVerts
      numSkipped++;
      chainActive = false;
      continue;
    }
    if (!merge || !chainActive || v0 != prevV0 + 1) {
      firstKey.push_back(int(keyVertex.size()));
      keyVertex.push_back(uint32_t(v0));
      curvePrim.push_back(uint32_t(i));
    }
    keyVertex.push_back(uint32_t(v0 + 1));
    chainActive = true;
    prevV0 = v0;
  }

  if (numSkipped > 0) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "curve geometry: skipped %zu segment(s) referencing out-of-range"
        " vertices",
        numSkipped);
  }
}

void Curve::setCurves(ccl::Hair *hair,
    const std::vector<int> &firstKey,
    const std::vector<uint32_t> &keyVertex) const
{
  ccl::array<ccl::float3> keys;
  ccl::array<float> radius;
  ccl::array<int> first;
  ccl::array<int> shader;

  auto *dstKey = keys.resize(keyVertex.size());
  auto *dstRadius = radius.resize(keyVertex.size());
  auto *dstFirst = first.resize(firstKey.size());
  auto *dstShader = shader.resize(firstKey.size());

  const auto *srcPoint = m_vertexPosition->beginAs<anari_vec::float3>();
  const float *srcRadius =
      m_vertexRadius ? m_vertexRadius->beginAs<float>() : nullptr;

  for (size_t k = 0; k < keyVertex.size(); k++) {
    const auto &pt = srcPoint[keyVertex[k]];
    dstKey[k] = make_float3(pt[0], pt[1], pt[2]);
    dstRadius[k] = srcRadius ? srcRadius[keyVertex[k]] : m_radius;
  }

  for (size_t c = 0; c < firstKey.size(); c++) {
    dstFirst[c] = firstKey[c];
    dstShader[c] = 0;
  }

  hair->set_curve_keys(keys);
  hair->set_curve_radius(radius);
  hair->set_curve_first_key(first);
  hair->set_curve_shader(shader);

  // Attributes added on a previous sync keep their old element count;
  // resize them to the new key count before setAttributes() writes them
  // (the mesh path gets this implicitly from resize_mesh()).
  hair->attributes.resize();
}

void Curve::setAttributes(ccl::Hair *hair,
    const std::vector<uint32_t> &keyVertex,
    const std::vector<uint32_t> &curvePrim) const
{
  auto &attrs = hair->attributes;

  auto keyOf = [&](size_t k) -> size_t { return keyVertex[k]; };
  auto primOf = [&](size_t c) -> size_t { return curvePrim[c]; };

  for (int c = 0; c < NUM_ATTRIBUTE_CHANNELS; c++) {
    if (m_vertexAttr[c]) {
      writeAttributeArray(attrs,
          c,
          ATTR_ELEMENT_CURVE_KEY,
          keyVertex.size(),
          *m_vertexAttr[c],
          keyOf);
    } else if (m_primitiveAttr[c]) {
      writeAttributeArray(attrs,
          c,
          ATTR_ELEMENT_CURVE,
          curvePrim.size(),
          *m_primitiveAttr[c],
          primOf);
    } else if (m_uniformAttr[c]) {
      writeAttributeConstant(attrs, c, *m_uniformAttr[c]);
    } else if (c == CH_COLOR) {
      writeAttributeConstant(attrs, c, DEFAULT_COLOR);
    } else {
      attrs.remove(ustring(CHANNEL_CYCLES_NAME[c]));
    }
  }

  writePrimitiveId(
      attrs, ATTR_ELEMENT_CURVE, curvePrim.size(), m_primitiveId.ptr, primOf);
}

// Cone/Cylinder definitions (tessellated tube meshes) ////////////////////////

// Cycles has no analytic cylinder/cone primitive, so both subtypes tessellate
// every segment into a triangle mesh: an N-gon lateral surface with analytic
// smooth normals plus optional flat end-cap disks. The side count is
// radius-independent; 32 sides keeps silhouettes smooth at typical primitive
// sizes while staying cheap (<= 128 triangles per fully-capped segment).
static constexpr uint32_t TUBE_NUM_SIDES = 32;

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
  helium::IntrusivePtr<Array1D> m_radiusArray; // primitive.radius/vertex.radius
  helium::IntrusivePtr<Array1D> m_vertexCap;
  float m_radius{1.f};
  std::string m_caps{"none"};
  RadiusSource m_radiusSource{RadiusSource::PER_PRIMITIVE};
  const char *m_subtype{"cylinder"};
};

Tube::Tube(CyclesGlobalState *s, RadiusSource radiusSource, const char *subtype)
    : Geometry(s),
      m_index(this),
      m_vertexPosition(this),
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

  writePrimitiveId(attrs, ATTR_ELEMENT_FACE, numTris, m_primitiveId.ptr, primOf);
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
      idx64 = (const uint64_t *)m_index->data();
    else // ANARI_UINT32_VEC2
      idx32 = (const uint32_t *)m_index->data();
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

///////////////////////////////////////////////////////////////////////////////
// Geometry definitions ///////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

struct UnknownGeometry : public Geometry
{
  UnknownGeometry(std::string_view subtype, CyclesGlobalState *s)
      : Geometry(s), m_subtype(subtype)
  {
    reportMessage(ANARI_SEVERITY_WARNING,
        "created unknown ANARI_GEOMETRY object of subtype '%s'",
        m_subtype.c_str());
  }

  bool isValid() const override
  {
    return false;
  }

  void warnIfUnknownObject() const override
  {
    reportMessage(ANARI_SEVERITY_WARNING,
        "encountered unknown ANARI_GEOMETRY object of subtype '%s'",
        m_subtype.c_str());
  }

  ccl::Geometry *createCyclesGeometryNode() override
  {
    return nullptr;
  }

  void syncCyclesNode(ccl::Geometry *) const override {}

 private:
  std::string m_subtype;
};

Geometry::Geometry(CyclesGlobalState *s) : Object(ANARI_GEOMETRY, s) {}

Geometry::~Geometry() = default;

Geometry *Geometry::createInstance(std::string_view type, CyclesGlobalState *s)
{
  if (type == "triangle")
    return new Mesh(s, false, "triangle");
  else if (type == "quad")
    return new Mesh(s, true, "quad");
  else if (type == "sphere")
    return new Sphere(s);
  else if (type == "curve")
    return new Curve(s);
  else if (type == "cylinder")
    return new Tube(s, Tube::RadiusSource::PER_PRIMITIVE, "cylinder");
  else if (type == "cone")
    return new Tube(s, Tube::RadiusSource::PER_VERTEX, "cone");
  else
    return new UnknownGeometry(type, s);
}

void Geometry::finalize()
{
  Object::finalize();
}

void Geometry::commitAttributeParameters()
{
  for (int c = 0; c < NUM_ATTRIBUTE_CHANNELS; c++) {
    const std::string suffix = CHANNEL_PARAM[c];
    m_uniformAttr[c].reset();
    anari_vec::float4 v = {0.f, 0.f, 0.f, 1.f};
    if (getParam(suffix, ANARI_FLOAT32_VEC4, &v))
      m_uniformAttr[c] = v;
    m_vertexAttr[c] = getParamObject<Array1D>("vertex." + suffix);
    m_primitiveAttr[c] = getParamObject<Array1D>("primitive." + suffix);
  }

  m_primitiveId = getParamObject<Array1D>("primitive.id");
  if (m_primitiveId && m_primitiveId->elementType() != ANARI_UINT32
      && m_primitiveId->elementType() != ANARI_UINT64) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'primitive.id' must be an array of UINT32 or UINT64 (got %s) "
        "-- ignoring",
        anari::toString(m_primitiveId->elementType()));
    m_primitiveId = nullptr;
  }
}

bool Geometry::hasPerPrimitiveAttributes() const
{
  if (m_primitiveId)
    return true;
  return std::any_of(m_primitiveAttr.begin(),
      m_primitiveAttr.end(),
      [](const auto &a) { return bool(a); });
}

helium::IntrusivePtr<Array1D> Geometry::validatedVertexPosition(
    const char *subtype)
{
  auto array = getParamObject<Array1D>("vertex.position");
  if (array && array->elementType() != ANARI_FLOAT32_VEC3) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'vertex.position' on %s geometry must be an array of FLOAT32_VEC3 "
        "(got %s) -- ignoring",
        subtype,
        anari::toString(array->elementType()));
    return {};
  }
  return array;
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Geometry *);
