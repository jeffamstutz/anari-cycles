// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Volume.h"
#include "spatial_field/SpatialField.h"
// cycles
#include "scene/mesh.h"
#include "scene/shader.h"
// std
#include <initializer_list>

namespace anari_cycles {

// Common base for volume subtypes rendered as a Cycles box proxy mesh (over
// the field bounds) carrying a volume shader.
struct FieldVolume : public Volume
{
  FieldVolume(CyclesGlobalState *s, const char *shaderName);
  ~FieldVolume() override;

  ccl::Geometry *cyclesGeometry() const override;
  box3 bounds() const override;

 protected:
  // (Re)build the scene-owned bounding-box mesh over m_bounds using m_shader.
  // 'fields' are the spatial fields sampled by the shader graph (null entries
  // allowed): rebuilding the mesh drops attached attributes, so each field
  // re-attaches its Cycles voxel-grid image (if any).
  void syncCyclesMesh(std::initializer_list<const SpatialField *> fields);
  // Retire the proxy mesh (invalid-volume path); scene->objects may still
  // reference it, so deletion is deferred.
  void retireMesh();
  // Scale the shader's ray marching step rate to roughly the field's voxel
  // size (without voxel grid attributes Cycles falls back to 1/10th of the
  // object bounds).
  void applyVolumeStepRate(const SpatialField *field);

  box3 m_bounds;
  ccl::Shader *m_shader{nullptr};
  ccl::Mesh *m_mesh{nullptr};
};

} // namespace anari_cycles
