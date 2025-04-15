// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

// cycles
#include "util/transform.h"
#include "util/types.h"
// anari
#include <anari/anari_cpp/ext/std.h>
#include <anari/anari_cpp.hpp>
// helium
#include <helium/helium_math.h>
// std
#include <limits>

namespace anari_cycles {

using namespace ccl;

namespace math = anari::math;

namespace anari_vec {

using namespace anari::std_types;

using float2 = vec2;
using float3 = vec3;
using float4 = vec4;
using uint3 = uvec3;

} // namespace anari_vec

// Types //////////////////////////////////////////////////////////////////////

template <typename T>
struct range_t
{
  T lower;
  T upper;
};

using box1 = range_t<float>;
using box2 = range_t<float2>;
using box3 = range_t<float3>;

// Functions //////////////////////////////////////////////////////////////////

inline box1 empty_box1()
{
  return {
      std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
}

inline box3 empty_box3()
{
  return {make_float3(std::numeric_limits<float>::max()),
      make_float3(-std::numeric_limits<float>::max())};
}

template <typename T>
inline void extend(range_t<T> &t, const T &v)
{
  t.lower = min(v, t.lower);
  t.upper = max(v, t.upper);
}

template <typename T>
inline void extend(range_t<T> &t1, const range_t<T> &t2)
{
  extend(t1, t2.lower);
  extend(t1, t2.upper);
}

inline float radians(float degrees)
{
  return degrees * float(M_PI) / 180.f;
}

inline float degrees(float radians)
{
  return radians * 180.f / float(M_PI);
}

inline ccl::Transform mat4ToCycles(const math::mat4 &m)
{
  ccl::Transform xfm;
  xfm.x.x = m[0].x;
  xfm.x.y = m[1].x;
  xfm.x.z = m[2].x;
  xfm.y.x = m[0].y;
  xfm.y.y = m[1].y;
  xfm.y.z = m[2].y;
  xfm.z.x = m[0].z;
  xfm.z.y = m[1].z;
  xfm.z.z = m[2].z;
  xfm.x.w = m[3].x;
  xfm.y.w = m[3].y;
  xfm.z.w = m[3].z;
  return xfm;
}

} // namespace anari_cycles

namespace anari {

ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::float2, ANARI_FLOAT32_VEC2);
ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::float3, ANARI_FLOAT32_VEC3);
ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::float4, ANARI_FLOAT32_VEC4);
ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::int2, ANARI_INT32_VEC2);
ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::int3, ANARI_INT32_VEC3);
ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::int4, ANARI_INT32_VEC4);
ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::uint2, ANARI_UINT32_VEC2);
ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::uint3, ANARI_UINT32_VEC3);
ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::uint4, ANARI_UINT32_VEC4);

#ifdef CYCLES_ANARI_DEFINITIONS
ANARI_TYPEFOR_DEFINITION(anari_cycles::float2);
ANARI_TYPEFOR_DEFINITION(anari_cycles::float3);
ANARI_TYPEFOR_DEFINITION(anari_cycles::float4);
ANARI_TYPEFOR_DEFINITION(anari_cycles::int2);
ANARI_TYPEFOR_DEFINITION(anari_cycles::int3);
ANARI_TYPEFOR_DEFINITION(anari_cycles::int4);
ANARI_TYPEFOR_DEFINITION(anari_cycles::uint2);
ANARI_TYPEFOR_DEFINITION(anari_cycles::uint3);
ANARI_TYPEFOR_DEFINITION(anari_cycles::uint4);
#endif

} // namespace anari
