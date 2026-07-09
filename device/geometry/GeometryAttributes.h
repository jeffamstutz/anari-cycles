// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"
// cycles
#include "scene/attribute.h"
// std
#include <algorithm>
#include <vector>

namespace anari_cycles {

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

inline const char *CHANNEL_PARAM[Geometry::NUM_ATTRIBUTE_CHANNELS] = {
    "color", "attribute0", "attribute1", "attribute2", "attribute3"};

inline const char *CHANNEL_CYCLES_NAME[Geometry::NUM_ATTRIBUTE_CHANNELS] = {
    "vertex.color",
    "vertex.attribute0",
    "vertex.attribute1",
    "vertex.attribute2",
    "vertex.attribute3"};

// Cycles' AttributeNode outputs (0,0,0) for absent attributes, but the ANARI
// default for the 'color' attribute is opaque white — geometries without any
// color source upload this constant instead of leaving the attribute absent.
inline constexpr anari_vec::float4 DEFAULT_COLOR = {1.f, 1.f, 1.f, 1.f};

// Convert an ANARI array to float4 once per source element (a source element
// commonly feeds many Cycles elements — corners, tessellated vertices — and
// the per-type dispatch is the expensive part of the conversion).
inline std::vector<anari_vec::float4> convertToFloat4(const Array1D &array)
{
  std::vector<anari_vec::float4> out(array.size());
  const void *src = array.begin(); // region-aware (KHR_ARRAY1D_REGION)
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
template <typename IndexFn>
inline void writeAttributeArray(ccl::AttributeSet &attrs,
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

  // All channels (including color) keep all four components: the alpha (4th)
  // component of the color source participates in the material's effective
  // opacity (see Material::connectAlpha). AttributeNode reads float4
  // attributes fine, exposing xyz as 'Color' and w as 'Alpha'.
  Attribute *attr = attrs.add(name, ccl::TypeFloat4, element);
  float4 *dst = attr->data_float4_for_write();
  for (size_t i = 0; i < count; i++) {
    const auto &c = converted[std::min<size_t>(srcIndex(i), maxIdx)];
    dst[i] = make_float4(c[0], c[1], c[2], c[3]);
  }
  attr->modified = true;
}

// Write a constant (uniform) attribute channel value as a per-geometry
// (ATTR_ELEMENT_MESH) attribute — a single value the kernel reads for every
// shading point on this geometry.
inline void writeAttributeConstant(
    ccl::AttributeSet &attrs, int channel, const anari_vec::float4 &v)
{
  const ustring name(CHANNEL_CYCLES_NAME[channel]);
  Attribute *attr = attrs.add(name, ccl::TypeFloat4, ATTR_ELEMENT_MESH);
  attr->data_float4_for_write()[0] = make_float4(v[0], v[1], v[2], v[3]);
  attr->modified = true;
}

// The ANARI 'primitiveId' attribute: primitive.id[prim] when the parameter is
// set, the primitive index itself otherwise. Cycles attributes are
// float-typed, so ids are exact up to 2^24.
template <typename IndexFn>
inline void writePrimitiveId(ccl::AttributeSet &attrs,
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
inline bool validNormalArray(
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
inline bool validTangentArray(
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

} // namespace anari_cycles
