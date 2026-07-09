// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Curve.h"
#include "GeometryAttributes.h"
// cycles
#include "scene/hair.h"
// std
#include <algorithm>

namespace anari_cycles {

Curve::Curve(CyclesGlobalState *s)
    : Geometry(s), m_index(this), m_vertexPosition(this), m_vertexRadius(this)
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
      attrs, ATTR_ELEMENT_CURVE, curvePrim.size(), m_primitiveId.get(), primOf);
}

} // namespace anari_cycles
