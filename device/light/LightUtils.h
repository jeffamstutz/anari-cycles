// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "cycles_math.h"
// anari
#include <anari/anari_cpp/ext/linalg.h>
// std
#include <cmath>

namespace anari_cycles {

inline math::mat4 rotationFromZNegativeToTarget(const math::float3 &targetDir)
{
  const math::float3 from = {0.0f, 0.0f, -1.0f};
  math::float3 to = math::normalize(targetDir);

  float cosTheta = math::dot(from, to);

  // If the directions are nearly the same
  if (std::abs(cosTheta - 1.0f) < 1e-6f)
    return linalg::identity;

  // If the directions are opposite
  if (std::abs(cosTheta + 1.0f) < 1e-6f) {
    // Find an arbitrary perpendicular axis to rotate 180° around
    math::float3 axis = math::cross(from, math::float3{1.0f, 0.0f, 0.0f});
    if (math::length(axis) < 1e-6f)
      axis = math::cross(from, math::float3{0.0f, 1.0f, 0.0f});
    axis = math::normalize(axis);

    float x = axis.x, y = axis.y, z = axis.z;
    return math::mat4{{1 - 2 * y * y - 2 * z * z, 2 * x * y, 2 * x * z, 0.f},
        {2 * x * y, 1 - 2 * x * x - 2 * z * z, 2 * y * z, 0.f},
        {2 * x * z, 2 * y * z, 1 - 2 * x * x - 2 * y * y, 0.f},
        {0.f, 0.f, 0.f, 1.f}};
  }

  // Otherwise, use Rodrigues' rotation formula
  math::float3 axis = math::normalize(math::cross(from, to));
  float s = std::sqrt(1.0f - cosTheta * cosTheta);
  math::mat3 K = {{0.0f, -axis.z, axis.y},
      {axis.z, 0.0f, -axis.x},
      {-axis.y, axis.x, 0.0f}};

  auto result = math::mat3{linalg::identity} + K * s
      + mul(K, K) * ((1.0f - cosTheta) / (s * s));

  return math::mat4{{result[0].x, result[0].y, result[0].z, 0.0f},
      {result[1].x, result[1].y, result[1].z, 0.0f},
      {result[2].x, result[2].y, result[2].z, 0.0f},
      {0.0f, 0.0f, 0.0f, 1.0f}};
}

// Transform for a light positioned at 'position' emitting along 'direction'
// (the Cycles light-local -Z axis).
inline math::mat4 positionDirectionXfm(
    const math::float3 &position, const math::float3 &direction)
{
  auto rot = math::inverse(rotationFromZNegativeToTarget(direction));
  rot[3] = {position.x, position.y, position.z, 1.f};
  return rot;
}

} // namespace anari_cycles
