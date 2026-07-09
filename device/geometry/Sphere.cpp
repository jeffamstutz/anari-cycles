// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Sphere.h"
#include "GeometryAttributes.h"
// cycles
#include "scene/pointcloud.h"
// std
#include <algorithm>

namespace anari_cycles {

Sphere::Sphere(CyclesGlobalState *s)
    : Geometry(s), m_index(this), m_vertexPosition(this), m_vertexRadius(this)
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

Sphere::VertexIndexer Sphere::vertexIndexer() const
{
  VertexIndexer indexer;
  if (m_index) {
    if (m_index->elementType() == ANARI_UINT64)
      indexer.idx64 = m_index->beginAs<uint64_t>();
    else
      indexer.idx32 = m_index->beginAs<uint32_t>();
  }
  return indexer;
}

size_t Sphere::numSpheres() const
{
  if (!m_vertexPosition)
    return 0;
  return m_index ? m_index->size() : m_vertexPosition->size();
}

box3 Sphere::bounds() const
{
  box3 b = empty_box3();
  if (!m_vertexPosition)
    return b;

  // Bounds must cover the sphere surfaces, not just their centers: consumers
  // (e.g. the CTS and anariRenderTests) place cameras from the world bounds,
  // so center-only bounds put the camera too close -- or, for a single
  // sphere, inside it. Only spheres referenced by 'primitive.index' count
  // (unused vertices must not inflate the bounds), mirroring Curve::bounds().
  const size_t n = numSpheres();
  const auto vertexOf = vertexIndexer();
  const auto *srcPoint = m_vertexPosition->beginAs<anari_vec::float3>();
  const float *srcRadius =
      m_vertexRadius ? m_vertexRadius->beginAs<float>() : nullptr;

  for (size_t i = 0; i < n; i++) {
    const size_t idx = vertexOf(i);
    const auto &v = srcPoint[idx];
    const float r = srcRadius ? srcRadius[idx] : m_radius;
    extend(b, make_float3(v[0] - r, v[1] - r, v[2] - r));
    extend(b, make_float3(v[0] + r, v[1] + r, v[2] + r));
  }
  return b;
}

void Sphere::setSpheres(ccl::PointCloud *pc) const
{
  ccl::array<ccl::float3> points;
  ccl::array<float> radius;
  ccl::array<int> shader;

  const size_t n = numSpheres();

  auto *dstPoint = (ccl::float3 *)points.resize(n);
  auto *dstRadius = (float *)radius.resize(n);
  auto *dstShader = (int *)shader.resize(n);

  const auto *srcPoint = m_vertexPosition
      ? m_vertexPosition->beginAs<anari_vec::float3>()
      : nullptr;
  const float *srcRadius = nullptr;
  if (m_vertexRadius)
    srcRadius = m_vertexRadius->beginAs<float>();

  const auto vertexOf = vertexIndexer();
  for (size_t i = 0; i < n; i++) {
    const size_t idx = vertexOf(i);
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
  const size_t n = numSpheres();

  // Each Cycles point is one ANARI primitive, so both vertex-rate (indexed
  // through 'primitive.index') and primitive-rate (direct) attributes land on
  // the per-point element.
  const auto vertexOf = vertexIndexer();
  auto identity = [](size_t i) { return i; };

  for (int c = 0; c < NUM_ATTRIBUTE_CHANNELS; c++) {
    if (m_vertexAttr[c]) {
      writeAttributeArray(
          attrs, c, ATTR_ELEMENT_VERTEX, n, *m_vertexAttr[c], vertexOf);
    } else if (m_primitiveAttr[c]) {
      writeAttributeArray(
          attrs, c, ATTR_ELEMENT_VERTEX, n, *m_primitiveAttr[c], identity);
    } else if (m_uniformAttr[c]) {
      writeAttributeConstant(attrs, c, *m_uniformAttr[c]);
    } else if (c == CH_COLOR) {
      writeAttributeConstant(attrs, c, DEFAULT_COLOR);
    } else {
      attrs.remove(ustring(CHANNEL_CYCLES_NAME[c]));
    }
  }

  writePrimitiveId(attrs, ATTR_ELEMENT_VERTEX, n, m_primitiveId.get(), identity);
}

} // namespace anari_cycles
