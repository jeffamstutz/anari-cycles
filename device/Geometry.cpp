// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Geometry.h"
// cycles
#include "scene/hair.h"
#include "scene/mesh.h"
#include "scene/pointcloud.h"
// std
#include <cmath>
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

// Shared mesh attribute helpers ///////////////////////////////////////////////

static void setMeshVertexNormal(
    ccl::Mesh *mesh, const helium::IntrusivePtr<Array1D> &array)
{
  if (!array)
    return;

  ustring name = ustring("vertex.normal");
  Attribute *attr = mesh->attributes.add(ATTR_STD_VERTEX_NORMAL, name);
  packed_normal *dst = attr->data_normal_for_write();
  std::transform(array->beginAs<anari_vec::float3>(),
      array->endAs<anari_vec::float3>(),
      dst,
      [](const anari_vec::float3 &v) {
        return packed_normal(make_float3(v[0], v[1], v[2]));
      });
}

static void setMeshVertexColor(
    ccl::Mesh *mesh, const helium::IntrusivePtr<Array1D> &array)
{
  if (!array)
    return;

  const void *src = array->data();
  anari::DataType type = array->elementType();

  Attribute *attr = mesh->attributes.add(
      ustring("vertex.color"), ccl::TypeColor, ATTR_ELEMENT_VERTEX);
  attr->std = ATTR_STD_VERTEX_COLOR;
  float3 *dst = attr->data_float3_for_write();
  for (uint32_t i = 0; i < array->size(); i++) {
    auto c = anari::anariTypeInvoke<anari_vec::float4, convert_toFloat4>(
        type, src, i);
    dst[i] = make_float3(c[0], c[1], c[2]);
  }
}

static void setMeshVertexAttribute(ccl::Mesh *mesh,
    const helium::IntrusivePtr<Array1D> &array,
    const char *name)
{
  if (!array)
    return;

  anari::DataType type = array->elementType();
  const void *src = array->data();

  Attribute *attr =
      mesh->attributes.add(ustring(name), ccl::TypeFloat4, ATTR_ELEMENT_VERTEX);
  float4 *dst = attr->data_float4_for_write();
  for (size_t i = 0; i < array->size(); i++) {
    auto r = anari::anariTypeInvoke<anari_vec::float4, convert_toFloat4>(
        type, src, i);
    dst[i].x = r[0];
    dst[i].y = r[1];
    dst[i].z = r[2];
    dst[i].w = r[3];
  }
}

// Triangle definitions ///////////////////////////////////////////////////////

struct Triangle : public Geometry
{
  Triangle(CyclesGlobalState *s);
  ~Triangle() override;

  void commitParameters() override;
  void finalize() override;

  ccl::Geometry *createCyclesGeometryNode() override;
  void syncCyclesNode(ccl::Geometry *node) const override;

  box3 bounds() const override;

 private:
  void setVertexPosition(ccl::Mesh *mesh) const;
  void setPrimitiveIndex(ccl::Mesh *mesh) const;

  helium::ChangeObserverPtr<Array1D> m_index;
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  helium::IntrusivePtr<Array1D> m_vertexNormal;
  helium::IntrusivePtr<Array1D> m_vertexColor;
  helium::IntrusivePtr<Array1D> m_vertexAttribute0;
  helium::IntrusivePtr<Array1D> m_vertexAttribute1;
  helium::IntrusivePtr<Array1D> m_vertexAttribute2;
  helium::IntrusivePtr<Array1D> m_vertexAttribute3;
};

Triangle::Triangle(CyclesGlobalState *s)
    : Geometry(s), m_index(this), m_vertexPosition(this)
{}

Triangle::~Triangle() = default;

void Triangle::commitParameters()
{
  Geometry::commitParameters();

  m_index = getParamObject<Array1D>("primitive.index");
  m_vertexPosition = getParamObject<Array1D>("vertex.position");
  m_vertexNormal = getParamObject<Array1D>("vertex.normal");
  m_vertexColor = getParamObject<Array1D>("vertex.color");
  m_vertexAttribute0 = getParamObject<Array1D>("vertex.attribute0");
  m_vertexAttribute1 = getParamObject<Array1D>("vertex.attribute1");
  m_vertexAttribute2 = getParamObject<Array1D>("vertex.attribute2");
  m_vertexAttribute3 = getParamObject<Array1D>("vertex.attribute3");
}

void Triangle::finalize()
{
  if (!m_vertexPosition) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'vertex.position' on triangle geometry");
  }

  Geometry::finalize();
}

ccl::Geometry *Triangle::createCyclesGeometryNode()
{
  return deviceState()->scene->create_node<ccl::Mesh>();
}

void Triangle::syncCyclesNode(ccl::Geometry *node) const
{
  auto *mesh = (ccl::Mesh *)node;

  if (!m_vertexPosition) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "Triangle::syncCyclesNode() detected incomplete geometry");
  }

  setVertexPosition(mesh);
  setPrimitiveIndex(mesh);
  setMeshVertexNormal(mesh, m_vertexNormal);
  setMeshVertexColor(mesh, m_vertexColor);
  setMeshVertexAttribute(mesh, m_vertexAttribute0, "vertex.attribute0");
  setMeshVertexAttribute(mesh, m_vertexAttribute1, "vertex.attribute1");
  setMeshVertexAttribute(mesh, m_vertexAttribute2, "vertex.attribute2");
  setMeshVertexAttribute(mesh, m_vertexAttribute3, "vertex.attribute3");
}

box3 Triangle::bounds() const
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

void Triangle::setVertexPosition(ccl::Mesh *mesh) const
{
  ccl::array<ccl::float3> P;
  auto *dst = P.resize(m_vertexPosition->size());
  std::transform(m_vertexPosition->beginAs<anari_vec::float3>(),
      m_vertexPosition->endAs<anari_vec::float3>(),
      dst,
      [](const anari_vec::float3 &v) { return make_float3(v[0], v[1], v[2]); });
  mesh->set_verts(P);
}

void Triangle::setPrimitiveIndex(ccl::Mesh *mesh) const
{
  const uint32_t numTriangles =
      m_index ? m_index->size() : m_vertexPosition->size() / 3;
  mesh->resize_mesh(m_vertexPosition->size(), numTriangles);
  auto *triangles = mesh->get_triangles().data();
  auto *shader = mesh->get_shader().data();
  auto *smooth = mesh->get_smooth().data();
  for (uint32_t i = 0; i < numTriangles; i++) {
    if (m_index) {
      auto *idxs = m_index->beginAs<anari_vec::uint3>();
      triangles[3 * i + 0] = idxs[i][0];
      triangles[3 * i + 1] = idxs[i][1];
      triangles[3 * i + 2] = idxs[i][2];
    } else {
      triangles[3 * i + 0] = 3 * i + 0;
      triangles[3 * i + 1] = 3 * i + 1;
      triangles[3 * i + 2] = 3 * i + 2;
    }
    shader[i] = 0;
    smooth[i] = true;
  }
  mesh->tag_triangles_modified();
  mesh->tag_shader_modified();
  mesh->tag_smooth_modified();
}

// Quad definitions ///////////////////////////////////////////////////////////

struct Quad : public Geometry
{
  Quad(CyclesGlobalState *s);
  ~Quad() override;

  void commitParameters() override;
  void finalize() override;

  ccl::Geometry *createCyclesGeometryNode() override;
  void syncCyclesNode(ccl::Geometry *node) const override;

  box3 bounds() const override;

 private:
  void setVertexPosition(ccl::Mesh *mesh) const;
  void setPrimitiveIndex(ccl::Mesh *mesh) const;

  helium::ChangeObserverPtr<Array1D> m_index;
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  helium::IntrusivePtr<Array1D> m_vertexNormal;
  helium::IntrusivePtr<Array1D> m_vertexColor;
  helium::IntrusivePtr<Array1D> m_vertexAttribute0;
  helium::IntrusivePtr<Array1D> m_vertexAttribute1;
  helium::IntrusivePtr<Array1D> m_vertexAttribute2;
  helium::IntrusivePtr<Array1D> m_vertexAttribute3;
};

Quad::Quad(CyclesGlobalState *s)
    : Geometry(s), m_index(this), m_vertexPosition(this)
{}

Quad::~Quad() = default;

void Quad::commitParameters()
{
  Geometry::commitParameters();

  m_index = getParamObject<Array1D>("primitive.index");
  m_vertexPosition = getParamObject<Array1D>("vertex.position");
  m_vertexNormal = getParamObject<Array1D>("vertex.normal");
  m_vertexColor = getParamObject<Array1D>("vertex.color");
  m_vertexAttribute0 = getParamObject<Array1D>("vertex.attribute0");
  m_vertexAttribute1 = getParamObject<Array1D>("vertex.attribute1");
  m_vertexAttribute2 = getParamObject<Array1D>("vertex.attribute2");
  m_vertexAttribute3 = getParamObject<Array1D>("vertex.attribute3");
}

void Quad::finalize()
{
  if (!m_vertexPosition) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'vertex.position' on quad geometry");
  }

  Geometry::finalize();
}

ccl::Geometry *Quad::createCyclesGeometryNode()
{
  return deviceState()->scene->create_node<ccl::Mesh>();
}

void Quad::syncCyclesNode(ccl::Geometry *node) const
{
  auto *mesh = (ccl::Mesh *)node;

  if (!m_vertexPosition) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "Quad::syncCyclesNode() detected incomplete geometry");
  }

  setVertexPosition(mesh);
  setPrimitiveIndex(mesh);
  setMeshVertexNormal(mesh, m_vertexNormal);
  setMeshVertexColor(mesh, m_vertexColor);
  setMeshVertexAttribute(mesh, m_vertexAttribute0, "vertex.attribute0");
  setMeshVertexAttribute(mesh, m_vertexAttribute1, "vertex.attribute1");
  setMeshVertexAttribute(mesh, m_vertexAttribute2, "vertex.attribute2");
  setMeshVertexAttribute(mesh, m_vertexAttribute3, "vertex.attribute3");
}

box3 Quad::bounds() const
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

void Quad::setVertexPosition(ccl::Mesh *mesh) const
{
  ccl::array<ccl::float3> P;
  auto *dst = P.resize(m_vertexPosition->size());
  std::transform(m_vertexPosition->beginAs<anari_vec::float3>(),
      m_vertexPosition->endAs<anari_vec::float3>(),
      dst,
      [](const anari_vec::float3 &v) { return make_float3(v[0], v[1], v[2]); });
  mesh->set_verts(P);
}

void Quad::setPrimitiveIndex(ccl::Mesh *mesh) const
{
  const uint32_t numQuads =
      m_index ? m_index->size() : m_vertexPosition->size() / 4;
  const uint32_t numTriangles = numQuads * 2;
  mesh->resize_mesh(m_vertexPosition->size(), numTriangles);
  auto *triangles = mesh->get_triangles().data();
  auto *shader = mesh->get_shader().data();
  auto *smooth = mesh->get_smooth().data();
  for (uint32_t i = 0; i < numQuads; i++) {
    uint32_t v0, v1, v2, v3;
    if (m_index) {
      auto *idxs = m_index->beginAs<anari_vec::uint4>();
      v0 = idxs[i][0];
      v1 = idxs[i][1];
      v2 = idxs[i][2];
      v3 = idxs[i][3];
    } else {
      v0 = 4 * i + 0;
      v1 = 4 * i + 1;
      v2 = 4 * i + 2;
      v3 = 4 * i + 3;
    }
    const uint32_t triangle = 2 * i;
    triangles[3 * triangle + 0] = v0;
    triangles[3 * triangle + 1] = v1;
    triangles[3 * triangle + 2] = v2;
    triangles[3 * triangle + 3] = v0;
    triangles[3 * triangle + 4] = v2;
    triangles[3 * triangle + 5] = v3;
    shader[triangle + 0] = 0;
    shader[triangle + 1] = 0;
    smooth[triangle + 0] = true;
    smooth[triangle + 1] = true;
  }
  mesh->tag_triangles_modified();
  mesh->tag_shader_modified();
  mesh->tag_smooth_modified();
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
  helium::IntrusivePtr<Array1D> m_vertexColor;
  helium::IntrusivePtr<Array1D> m_vertexAttribute0;
  helium::IntrusivePtr<Array1D> m_vertexAttribute1;
  helium::IntrusivePtr<Array1D> m_vertexAttribute2;
  helium::IntrusivePtr<Array1D> m_vertexAttribute3;
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

  m_index = getParamObject<Array1D>("primitive.index");
  m_vertexPosition = getParamObject<Array1D>("vertex.position");
  m_vertexColor = getParamObject<Array1D>("vertex.color");
  m_vertexAttribute0 = getParamObject<Array1D>("vertex.attribute0");
  m_vertexAttribute1 = getParamObject<Array1D>("vertex.attribute1");
  m_vertexAttribute2 = getParamObject<Array1D>("vertex.attribute2");
  m_vertexAttribute3 = getParamObject<Array1D>("vertex.attribute3");
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

  size_t numSpheres = m_index ? m_index->size() : m_vertexPosition->size();

  auto *dstPoint = (ccl::float3 *)points.resize(numSpheres);
  auto *dstRadius = (float *)radius.resize(numSpheres);
  auto *dstShader = (int *)shader.resize(numSpheres);

  const auto *srcPoint = m_vertexPosition->beginAs<anari_vec::float3>();
  const float *srcRadius = nullptr;
  if (m_vertexRadius)
    srcRadius = m_vertexRadius->beginAs<float>();

  const uint32_t *srcIdx = nullptr;
  if (m_index)
    srcIdx = m_index->beginAs<uint32_t>();

  for (size_t i = 0; i < numSpheres; i++) {
    size_t idx = srcIdx ? size_t(srcIdx[i]) : i;
    const auto &pt = srcPoint[idx];
    dstPoint[i] = make_float3(pt[0], pt[1], pt[2]);
    dstRadius[i] = srcRadius ? srcRadius[idx] : m_radius;
    dstShader[i] = 0;
  }

  pc->set_points(points);
  pc->set_radius(radius);
  pc->set_shader(shader);
}

void Sphere::setAttributes(ccl::PointCloud *pc) const
{
  float3 *dstC = nullptr;
  float3 *dst0 = nullptr;
  float3 *dst1 = nullptr;
  float3 *dst2 = nullptr;
  float3 *dst3 = nullptr;

  const void *srcC = nullptr;
  const void *src0 = nullptr;
  const void *src1 = nullptr;
  const void *src2 = nullptr;
  const void *src3 = nullptr;

  anari::DataType srcTC = ANARI_UNKNOWN;
  anari::DataType srcT0 = ANARI_UNKNOWN;
  anari::DataType srcT1 = ANARI_UNKNOWN;
  anari::DataType srcT2 = ANARI_UNKNOWN;
  anari::DataType srcT3 = ANARI_UNKNOWN;

  size_t numSpheres = m_index ? m_index->size() : m_vertexPosition->size();

  if (m_vertexColor) {
    Attribute *attr = pc->attributes.add(
        ustring("vertex.color"), ccl::TypeColor, ATTR_ELEMENT_VERTEX);
    attr->std = ATTR_STD_VERTEX_COLOR;
    dstC = attr->data_float3_for_write();
    srcC = m_vertexColor->data();
    srcTC = m_vertexColor->elementType();
  }

  if (m_vertexAttribute0) {
    Attribute *attr = pc->attributes.add(
        ustring("vertex.attribute0"), ccl::TypeColor, ATTR_ELEMENT_VERTEX);
    dst0 = attr->data_float3_for_write();
    src0 = m_vertexAttribute0->data();
    srcT0 = m_vertexAttribute0->elementType();
  }

  if (m_vertexAttribute1) {
    Attribute *attr = pc->attributes.add(
        ustring("vertex.attribute1"), ccl::TypeColor, ATTR_ELEMENT_VERTEX);
    dst1 = attr->data_float3_for_write();
    src1 = m_vertexAttribute1->data();
    srcT1 = m_vertexAttribute1->elementType();
  }

  if (m_vertexAttribute2) {
    Attribute *attr = pc->attributes.add(
        ustring("vertex.attribute2"), ccl::TypeColor, ATTR_ELEMENT_VERTEX);
    dst2 = attr->data_float3_for_write();
    src2 = m_vertexAttribute2->data();
    srcT2 = m_vertexAttribute2->elementType();
  }

  if (m_vertexAttribute3) {
    Attribute *attr = pc->attributes.add(
        ustring("vertex.attribute3"), ccl::TypeColor, ATTR_ELEMENT_VERTEX);
    dst3 = attr->data_float3_for_write();
    src3 = m_vertexAttribute3->data();
    srcT3 = m_vertexAttribute3->elementType();
  }

  const uint32_t *srcIdx = nullptr;
  if (m_index)
    srcIdx = m_index->beginAs<uint32_t>();
  for (size_t i = 0; i < numSpheres; i++) {
    size_t idx = srcIdx ? size_t(srcIdx[i]) : i;
    if (dstC) {
      auto c = anari::anariTypeInvoke<anari_vec::float4, convert_toFloat4>(
          srcTC, srcC, idx);
      dstC[i] = make_float3(c[0], c[1], c[2]);
    }

    if (dst0) {
      auto c = anari::anariTypeInvoke<anari_vec::float4, convert_toFloat4>(
          srcT0, src0, idx);
      dst0[i] = make_float3(c[0], c[1], c[2]);
    }

    if (dst1) {
      auto c = anari::anariTypeInvoke<anari_vec::float4, convert_toFloat4>(
          srcT1, src1, idx);
      dst1[i] = make_float3(c[0], c[1], c[2]);
    }

    if (dst2) {
      auto c = anari::anariTypeInvoke<anari_vec::float4, convert_toFloat4>(
          srcT2, src2, idx);
      dst2[i] = make_float3(c[0], c[1], c[2]);
    }

    if (dst3) {
      auto c = anari::anariTypeInvoke<anari_vec::float4, convert_toFloat4>(
          srcT3, src3, idx);
      dst3[i] = make_float3(c[0], c[1], c[2]);
    }
  }
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
  // first key of Cycles curve c.
  void computeCurveLayout(
      std::vector<int> &firstKey, std::vector<uint32_t> &keyVertex) const;
  void setCurves(ccl::Hair *hair,
      const std::vector<int> &firstKey,
      const std::vector<uint32_t> &keyVertex) const;
  void setAttributes(
      ccl::Hair *hair, const std::vector<uint32_t> &keyVertex) const;

  helium::ChangeObserverPtr<Array1D> m_index;
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  helium::IntrusivePtr<Array1D> m_vertexColor;
  helium::IntrusivePtr<Array1D> m_vertexAttribute0;
  helium::IntrusivePtr<Array1D> m_vertexAttribute1;
  helium::IntrusivePtr<Array1D> m_vertexAttribute2;
  helium::IntrusivePtr<Array1D> m_vertexAttribute3;
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

  m_index = getParamObject<Array1D>("primitive.index");
  m_vertexPosition = getParamObject<Array1D>("vertex.position");
  m_vertexColor = getParamObject<Array1D>("vertex.color");
  m_vertexAttribute0 = getParamObject<Array1D>("vertex.attribute0");
  m_vertexAttribute1 = getParamObject<Array1D>("vertex.attribute1");
  m_vertexAttribute2 = getParamObject<Array1D>("vertex.attribute2");
  m_vertexAttribute3 = getParamObject<Array1D>("vertex.attribute3");
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
  computeCurveLayout(firstKey, keyVertex);

  setCurves(hair, firstKey, keyVertex);
  setAttributes(hair, keyVertex);
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
  computeCurveLayout(firstKey, keyVertex);

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

void Curve::computeCurveLayout(
    std::vector<int> &firstKey, std::vector<uint32_t> &keyVertex) const
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

  // Runs of consecutive segments sharing a vertex ((a,a+1),(a+1,a+2),...)
  // merge into one multi-key Cycles curve. Thick-linear curves have spherical
  // end caps, so the union of per-segment 2-key curves is geometrically
  // identical; merging just shares the interior keys.
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
    if (!chainActive || v0 != prevV0 + 1) {
      firstKey.push_back(int(keyVertex.size()));
      keyVertex.push_back(uint32_t(v0));
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

void Curve::setAttributes(
    ccl::Hair *hair, const std::vector<uint32_t> &keyVertex) const
{
  auto setCurveKeyAttribute = [&](const helium::IntrusivePtr<Array1D> &array,
                                  const char *name,
                                  bool isColor) {
    if (!array) {
      // drop stale data if the parameter was removed since the last sync
      hair->attributes.remove(ustring(name));
      return;
    }

    const void *src = array->data();
    anari::DataType type = array->elementType();

    if (isColor) {
      Attribute *attr = hair->attributes.add(
          ustring(name), ccl::TypeColor, ATTR_ELEMENT_CURVE_KEY);
      attr->std = ATTR_STD_VERTEX_COLOR;
      float3 *dst = attr->data_float3_for_write();
      for (size_t k = 0; k < keyVertex.size(); k++) {
        auto c = anari::anariTypeInvoke<anari_vec::float4, convert_toFloat4>(
            type, src, keyVertex[k]);
        dst[k] = make_float3(c[0], c[1], c[2]);
      }
    } else {
      Attribute *attr = hair->attributes.add(
          ustring(name), ccl::TypeFloat4, ATTR_ELEMENT_CURVE_KEY);
      float4 *dst = attr->data_float4_for_write();
      for (size_t k = 0; k < keyVertex.size(); k++) {
        auto c = anari::anariTypeInvoke<anari_vec::float4, convert_toFloat4>(
            type, src, keyVertex[k]);
        dst[k] = make_float4(c[0], c[1], c[2], c[3]);
      }
    }
  };

  setCurveKeyAttribute(m_vertexColor, "vertex.color", true);
  setCurveKeyAttribute(m_vertexAttribute0, "vertex.attribute0", false);
  setCurveKeyAttribute(m_vertexAttribute1, "vertex.attribute1", false);
  setCurveKeyAttribute(m_vertexAttribute2, "vertex.attribute2", false);
  setCurveKeyAttribute(m_vertexAttribute3, "vertex.attribute3", false);
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
  helium::IntrusivePtr<Array1D> m_vertexColor;
  helium::IntrusivePtr<Array1D> m_vertexAttribute0;
  helium::IntrusivePtr<Array1D> m_vertexAttribute1;
  helium::IntrusivePtr<Array1D> m_vertexAttribute2;
  helium::IntrusivePtr<Array1D> m_vertexAttribute3;
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

  m_index = getParamObject<Array1D>("primitive.index");
  m_vertexPosition = getParamObject<Array1D>("vertex.position");
  m_vertexColor = getParamObject<Array1D>("vertex.color");
  m_vertexAttribute0 = getParamObject<Array1D>("vertex.attribute0");
  m_vertexAttribute1 = getParamObject<Array1D>("vertex.attribute1");
  m_vertexAttribute2 = getParamObject<Array1D>("vertex.attribute2");
  m_vertexAttribute3 = getParamObject<Array1D>("vertex.attribute3");
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
  }

  // Each generated vertex inherits the attributes of its source ANARI vertex.
  auto setTubeVertexAttribute = [&](const helium::IntrusivePtr<Array1D> &array,
                                    const char *name,
                                    bool isColor) {
    if (!array || array->size() == 0) {
      // drop stale data if the parameter was removed since the last sync
      mesh->attributes.remove(ustring(name));
      return;
    }

    const void *src = array->data();
    anari::DataType type = array->elementType();
    const size_t maxIdx = array->size() - 1;

    // Convert once per source vertex (a generated vertex maps to one of only
    // two source vertices per segment; converting per generated vertex would
    // re-run the type dispatch 32-65x per source element).
    std::vector<anari_vec::float4> converted(array->size());
    for (size_t i = 0; i < converted.size(); i++) {
      converted[i] = anari::anariTypeInvoke<anari_vec::float4,
          convert_toFloat4>(type, src, i);
    }

    if (isColor) {
      Attribute *attr = mesh->attributes.add(
          ustring(name), ccl::TypeColor, ATTR_ELEMENT_VERTEX);
      attr->std = ATTR_STD_VERTEX_COLOR;
      float3 *dst = attr->data_float3_for_write();
      for (size_t i = 0; i < numVerts; i++) {
        const auto &c = converted[std::min<size_t>(md.srcVertex[i], maxIdx)];
        dst[i] = make_float3(c[0], c[1], c[2]);
      }
    } else {
      Attribute *attr = mesh->attributes.add(
          ustring(name), ccl::TypeFloat4, ATTR_ELEMENT_VERTEX);
      float4 *dst = attr->data_float4_for_write();
      for (size_t i = 0; i < numVerts; i++) {
        const auto &c = converted[std::min<size_t>(md.srcVertex[i], maxIdx)];
        dst[i] = make_float4(c[0], c[1], c[2], c[3]);
      }
    }
  };

  setTubeVertexAttribute(m_vertexColor, "vertex.color", true);
  setTubeVertexAttribute(m_vertexAttribute0, "vertex.attribute0", false);
  setTubeVertexAttribute(m_vertexAttribute1, "vertex.attribute1", false);
  setTubeVertexAttribute(m_vertexAttribute2, "vertex.attribute2", false);
  setTubeVertexAttribute(m_vertexAttribute3, "vertex.attribute3", false);
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
    md.tris.reserve(numSegments * trisPerSeg * 3);
    md.smooth.reserve(numSegments * trisPerSeg);
  }

  const size_t numSkipped = forEachSegment(
      [&](size_t,
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
          md.tris.insert(md.tris.end(), {a0, b1, a1});
          md.smooth.push_back(true);
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
    return new Triangle(s);
  else if (type == "quad")
    return new Quad(s);
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

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Geometry *);
