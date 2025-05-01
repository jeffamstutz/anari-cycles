// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Geometry.h"
// cycles
#include "scene/mesh.h"
#include "scene/pointcloud.h"

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
  void setVertexNormal(ccl::Mesh *mesh) const;
  void setVertexColor(ccl::Mesh *mesh) const;
  void setVertexAttribute(ccl::Mesh *mesh,
      const helium::IntrusivePtr<Array1D> &array,
      const char *name) const;

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
  setVertexNormal(mesh);
  setVertexColor(mesh);
  setVertexAttribute(mesh, m_vertexAttribute0, "vertex.attribute0");
  setVertexAttribute(mesh, m_vertexAttribute1, "vertex.attribute1");
  setVertexAttribute(mesh, m_vertexAttribute2, "vertex.attribute2");
  setVertexAttribute(mesh, m_vertexAttribute3, "vertex.attribute3");
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
  mesh->reserve_mesh(numTriangles * 3, numTriangles);
  for (uint32_t i = 0; i < numTriangles; i++) {
    if (m_index) {
      auto *idxs = m_index->beginAs<anari_vec::uint3>();
      mesh->add_triangle(
          idxs[i][0], idxs[i][1], idxs[i][2], 0 /* local shaderID */, true);
    } else {
      mesh->add_triangle(
          3 * i + 0, 3 * i + 1, 3 * i + 2, 0 /* local shaderID */, true);
    }
  }
}

void Triangle::setVertexNormal(ccl::Mesh *mesh) const
{
  if (!m_vertexNormal)
    return;

  ustring name = ustring("vertex.normal");
  Attribute *attr = mesh->attributes.add(ATTR_STD_VERTEX_NORMAL, name);
  float3 *dst = attr->data_float3();
  std::transform(m_vertexNormal->beginAs<anari_vec::float3>(),
      m_vertexNormal->endAs<anari_vec::float3>(),
      dst,
      [](const anari_vec::float3 &v) { return make_float3(v[0], v[1], v[2]); });
}

void Triangle::setVertexColor(ccl::Mesh *mesh) const
{
  auto &array = m_vertexColor;
  if (!array)
    return;

  const void *src = array->data();
  anari::DataType type = array->elementType();

  Attribute *attr = mesh->attributes.add(
      ustring("vertex.color"), TypeDesc::TypeColor, ATTR_ELEMENT_VERTEX);
  attr->std = ATTR_STD_VERTEX_COLOR;
  float3 *dst = attr->data_float3();
  for (uint32_t i = 0; i < array->size(); i++) {
    auto c = anari::anariTypeInvoke<anari_vec::float4, convert_toFloat4>(
        type, src, i);
    dst[i] = make_float3(c[0], c[1], c[2]);
  }
}

void Triangle::setVertexAttribute(ccl::Mesh *mesh,
    const helium::IntrusivePtr<Array1D> &array,
    const char *name) const
{
  if (!array)
    return;

  anari::DataType type = array->elementType();
  const void *src = array->data();

  Attribute *attr = mesh->attributes.add(ATTR_STD_UV, ustring(name));
  float2 *dst = attr->data_float2();
  size_t i = 0;
  std::for_each(dst, dst + m_vertexPosition->size(), [&](float2 &v) {
    auto r = anari::anariTypeInvoke<anari_vec::float4, convert_toFloat4>(
        type, src, i);
    v.x = r[0];
    v.y = r[1];
    i++;
  });
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
        ustring("vertex.color"), TypeDesc::TypeColor, ATTR_ELEMENT_VERTEX);
    attr->std = ATTR_STD_VERTEX_COLOR;
    dstC = attr->data_float3();
    srcC = m_vertexColor->data();
    srcTC = m_vertexColor->elementType();
  }

  if (m_vertexAttribute0) {
    Attribute *attr = pc->attributes.add(
        ustring("vertex.attribute0"), TypeDesc::TypeColor, ATTR_ELEMENT_VERTEX);
    attr->std = ATTR_STD_VERTEX_COLOR;
    dst0 = attr->data_float3();
    src0 = m_vertexAttribute0->data();
    srcT0 = m_vertexAttribute0->elementType();
  }

  if (m_vertexAttribute1) {
    Attribute *attr = pc->attributes.add(
        ustring("vertex.attribute1"), TypeDesc::TypeColor, ATTR_ELEMENT_VERTEX);
    attr->std = ATTR_STD_VERTEX_COLOR;
    dst1 = attr->data_float3();
    src1 = m_vertexAttribute1->data();
    srcT1 = m_vertexAttribute1->elementType();
  }

  if (m_vertexAttribute2) {
    Attribute *attr = pc->attributes.add(
        ustring("vertex.attribute2"), TypeDesc::TypeColor, ATTR_ELEMENT_VERTEX);
    attr->std = ATTR_STD_VERTEX_COLOR;
    dst2 = attr->data_float3();
    src2 = m_vertexAttribute2->data();
    srcT2 = m_vertexAttribute2->elementType();
  }

  if (m_vertexAttribute3) {
    Attribute *attr = pc->attributes.add(
        ustring("vertex.attribute3"), TypeDesc::TypeColor, ATTR_ELEMENT_VERTEX);
    attr->std = ATTR_STD_VERTEX_COLOR;
    dst3 = attr->data_float3();
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

///////////////////////////////////////////////////////////////////////////////
// Geometry definitions ///////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

Geometry::Geometry(CyclesGlobalState *s) : Object(ANARI_GEOMETRY, s) {}

Geometry::~Geometry() = default;

Geometry *Geometry::createInstance(std::string_view type, CyclesGlobalState *s)
{
  if (type == "triangle")
    return new Triangle(s);
  else if (type == "sphere")
    return new Sphere(s);
  else
    return (Geometry *)new UnknownObject(ANARI_GEOMETRY, type, s);
}

void Geometry::finalize()
{
  Object::finalize();
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Geometry *);
