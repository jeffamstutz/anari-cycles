// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "array/Array1D.h"
#include "Object.h"
#include "cycles_math.h"
// cycles
#include "scene/object.h" // ccl::Object::MAX_MOTION_STEPS
// helium
#include "helium/helium_math.h"
// std
#include <algorithm>
#include <cstddef>
#include <vector>

namespace anari_cycles {

// A time-varying rigid(ish) transform described by the ANARI motion
// extensions (KHR_INSTANCE_MOTION_TRANSFORM,
// KHR_INSTANCE_MOTION_SCALE_ROTATION_TRANSLATION,
// KHR_CAMERA_MOTION_TRANSFORMATION): key arrays uniformly distributed over
// the 'time' interval. When 'matrix' keys are present they take precedence
// over the scale/rotation/translation keys (matching how the device
// prioritizes 'motion.transform').
//
// Sampling semantics (documented resampling contract):
//  * Keys are interpolated per segment the same way Cycles interpolates its
//    baked motion steps: matrix keys are decomposed and their components
//    interpolated (rotation slerp) via Cycles' transform_motion_* utilities
//    (a componentwise matrix lerp would shrink/skew rotating keys); SRT keys
//    lerp scale/translation and slerp rotation. ANARI prescribes uniformly
//    distributed keys with no interpolation mode, so this per-segment
//    interpolation is the documented reconstruction.
//  * Sampling outside [time.lower, time.upper] clamps to the end keys.
//  * Resampling onto the camera shutter (bakeMotionOnShutter()) is exact at
//    sample times that hit source keys. When the shutter equals the 'time'
//    interval the baked steps land exactly on the keys. When the shutter is
//    a strict sub-interval, source keys can fall strictly inside a baked
//    segment where the Cycles kernel interpolates straight across them; the
//    bake refines its step count in that case to keep the deviation small
//    (exact alignment is impossible in general).
struct MotionTrack
{
  std::vector<math::mat4> matrix;
  std::vector<math::float3> scale;
  std::vector<math::float4> rotation; // quaternion (i,j,k,w)
  std::vector<math::float3> translation;
  helium::box1 time{0.f, 1.f};

  bool empty() const
  {
    return matrix.empty() && scale.empty() && rotation.empty()
        && translation.empty();
  }

  size_t numKeys() const
  {
    return std::max(std::max(matrix.size(), scale.size()),
        std::max(rotation.size(), translation.size()));
  }

  // 'true' when every key array holds a single repeated value -- the track
  // cannot produce motion (exact compare on the app-supplied keys, catching
  // what interpolation rounding in the baked steps might miss).
  bool isConstant() const
  {
    auto allSame = [](const auto &keys) {
      for (size_t i = 1; i < keys.size(); i++) {
        if (!(keys[i] == keys[0]))
          return false;
      }
      return true;
    };
    return allSame(matrix) && allSame(scale) && allSame(rotation)
        && allSame(translation);
  }

  math::mat4 sample(float t) const;
};

// Validated copy of a motion key array into a std::vector (empty result +
// warning when the element type does not match the extension spec).
template <typename T>
inline std::vector<T> readMotionKeys(const Object *obj,
    const Array1D *array,
    anari::DataType expected,
    const char *param)
{
  std::vector<T> keys;
  if (!array)
    return keys;
  if (array->elementType() != expected) {
    obj->reportMessage(ANARI_SEVERITY_WARNING,
        "'%s' must be an array of %s (got %s) -- ignoring",
        param,
        anari::toString(expected),
        anari::toString(array->elementType()));
    return keys;
  }
  auto *begin = (const T *)array->begin();
  keys.assign(begin, begin + array->size());
  return keys;
}

namespace detail {

// Map absolute frame time 't' onto key array of size 'm' spanning 'time':
// returns segment index 'j' and fraction 'f' with the sample between keys
// j and j+1. Degenerate cases (single key, empty/degenerate time interval)
// resolve to the first key.
inline void keyLocation(
    const helium::box1 &time, size_t m, float t, size_t &j, float &f)
{
  j = 0;
  f = 0.f;
  if (m < 2)
    return;
  const float extent = time.upper - time.lower;
  if (!(extent > 0.f))
    return;
  const float u = std::clamp((t - time.lower) / extent, 0.f, 1.f);
  const float x = u * float(m - 1);
  j = std::min(size_t(x), m - 2);
  f = x - float(j);
}

template <typename T>
inline T lerpKeys(const std::vector<T> &keys,
    const helium::box1 &time,
    float t,
    const T &fallback)
{
  if (keys.empty())
    return fallback;
  size_t j;
  float f;
  keyLocation(time, keys.size(), t, j, f);
  if (f == 0.f)
    return keys[j];
  return keys[j] * (1.f - f) + keys[j + 1] * f;
}

} // namespace detail

inline math::mat4 MotionTrack::sample(float t) const
{
  if (!matrix.empty()) {
    size_t j;
    float f;
    detail::keyLocation(time, matrix.size(), t, j, f);
    if (f == 0.f)
      return matrix[j];
    // Interpolate exactly the way the Cycles kernel interpolates baked
    // motion steps: decompose the bracketing keys (rotation as quaternion,
    // scale/shear separated) and blend the components. A componentwise
    // matrix lerp would shrink/skew any rotating key pair -- e.g. the
    // default degenerate shutter [0.5,0.5] samples the track midpoint,
    // which must be the mid-rotation pose, not an averaged matrix.
    const ccl::Transform pair[2] = {
        mat4ToCycles(matrix[j]), mat4ToCycles(matrix[j + 1])};
    ccl::DecomposedTransform decomp[2];
    ccl::transform_motion_decompose(decomp, pair, 2);
    ccl::Transform out;
    ccl::transform_motion_array_interpolate(&out, decomp, 2, f);
    return cyclesToMat4(out);
  }

  const auto s =
      detail::lerpKeys(scale, time, t, math::float3(1.f, 1.f, 1.f));
  const auto q = [&]() -> math::float4 {
    const math::float4 identityQuat(0.f, 0.f, 0.f, 1.f);
    if (rotation.empty())
      return identityQuat;
    size_t j;
    float f;
    detail::keyLocation(time, rotation.size(), t, j, f);
    if (f == 0.f)
      return rotation[j];
    return linalg::qslerp(rotation[j], rotation[j + 1], f);
  }();
  const auto p =
      detail::lerpKeys(translation, time, t, math::float3(0.f, 0.f, 0.f));

  return linalg::mul(math::translation_matrix(p),
      linalg::mul(math::rotation_matrix(q), math::scaling_matrix(s)));
}

// Bake the track onto the camera shutter interval as Cycles motion steps,
// honoring the shutter contract established by Camera::setCameraCurrent():
// Cycles' kernel ray-time domain [0,1] corresponds exactly to the ANARI
// shutter interval [s0,s1], so motion step i of N is sampled at frame time
// s0 + (s1-s0) * i/(N-1). N matches the source key count when the shutter
// spans the whole 'time' interval (steps land exactly on the keys) and is
// refined 4x otherwise (see the resampling contract above); always >= 2 and
// capped at Cycles' step limit.
inline std::vector<ccl::Transform> bakeMotionOnShutter(
    const MotionTrack &track, const helium::box1 &shutter)
{
  std::vector<ccl::Transform> steps;
  const float extent = shutter.upper - shutter.lower;
  if (track.empty() || track.isConstant() || !(extent > 0.f))
    return steps; // degenerate/no motion -- caller uses a single sample

  const size_t maxSteps = size_t(ccl::Object::MAX_MOTION_STEPS);
  size_t n = std::clamp(track.numKeys(), size_t(2), maxSteps);
  const bool aligned = shutter.lower == track.time.lower
      && shutter.upper == track.time.upper;
  if (!aligned)
    n = std::min(maxSteps, (n - 1) * 4 + 1);

  steps.reserve(n);
  bool allEqual = true;
  for (size_t i = 0; i < n; i++) {
    const float t = shutter.lower + extent * float(i) / float(n - 1);
    steps.push_back(mat4ToCycles(track.sample(t)));
    allEqual = allEqual && steps.back() == steps.front();
  }

  // No actual motion across the shutter -- keep the machinery off so such
  // scenes render exactly as static ones.
  if (allEqual)
    steps.clear();

  return steps;
}

} // namespace anari_cycles
