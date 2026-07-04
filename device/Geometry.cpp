// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Geometry.h"
// cycles
#include "scene/hair.h"
#include "scene/mesh.h"
#include "scene/pointcloud.h"
// std
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
  else
    return new UnknownGeometry(type, s);
}

void Geometry::finalize()
{
  Object::finalize();
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Geometry *);
