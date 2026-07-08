// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Array.h"
#include "Object.h"
// cycles
#include "scene/geometry.h"
// helium
#include "helium/helium_math.h"
// std
#include <array>
#include <optional>

namespace anari_cycles {

struct Geometry : public Object
{
  Geometry(CyclesGlobalState *s);
  ~Geometry() override;

  static Geometry *createInstance(
      std::string_view type, CyclesGlobalState *state);

  virtual void finalize() override;

  virtual ccl::Geometry *createCyclesGeometryNode() = 0;
  virtual void syncCyclesNode(ccl::Geometry *node) const = 0;

  // KHR_GEOMETRY_TRIANGLE/QUAD_MOTION_DEFORMATION //

  // 'true' when this geometry carries deformation motion keys (a nested
  // 'vertex.position' array-of-key-arrays with >= 2 keys), i.e. its baked
  // Cycles node depends on the camera shutter interval and must be re-baked
  // through bakeDeformationMotion() whenever that interval changes.
  virtual bool hasDeformationMotion() const;

  // Re-bake the deformation keys onto 'shutter' (per the shutter contract in
  // MotionTrack.h: Cycles kernel ray-time [0,1] spans exactly [s0,s1], so
  // motion step i of N samples the keys at s0 + (s1-s0)*i/(N-1)). Writes the
  // node's base vertices (the shutter-midpoint pose -- Cycles' center step)
  // plus the ATTR_STD_MOTION_VERTEX_POSITION/_NORMAL attributes. Returns
  // 'true' when motion steps are active on the node afterwards (the caller
  // then enables integrator motion blur); a degenerate shutter or keys that
  // do not move across it collapse to a sharp midpoint pose and return
  // 'false'. Called by Surface::bakeGeometryMotion() during world rebuilds.
  virtual bool bakeDeformationMotion(
      ccl::Geometry *node, const helium::box1 &shutter) const;

  // The five general ANARI attribute channels: color, attribute0..attribute3.
  static constexpr int NUM_ATTRIBUTE_CHANNELS = 5;

 protected:
  // Reads the attribute-channel parameters shared by every subtype: uniform
  // values ('color', 'attributeN'), per-vertex arrays ('vertex.color', ...),
  // per-primitive arrays ('primitive.color', ..., 'primitive.id').
  void commitAttributeParameters();

  // 'true' when any per-primitive attribute source ('primitive.color',
  // 'primitive.attributeN' or 'primitive.id') is present.
  bool hasPerPrimitiveAttributes() const;

  // Fetch 'vertex.position', rejecting (with a warning) arrays that are not
  // ANARI_FLOAT32_VEC3 as the spec requires.
  helium::IntrusivePtr<Array1D> validatedVertexPosition(const char *subtype);

  // Attribute arrays are held through ChangeObserverPtr so committing a
  // change on the array itself (new data via map/unmap, or a new 'region' --
  // KHR_ARRAY1D_REGION) re-finalizes this geometry and, through its own
  // change observers, the surfaces that sync it to Cycles.
  std::array<helium::ChangeObserverPtr<Array1D>, NUM_ATTRIBUTE_CHANNELS>
      m_vertexAttr;
  std::array<helium::ChangeObserverPtr<Array1D>, NUM_ATTRIBUTE_CHANNELS>
      m_primitiveAttr;
  std::array<std::optional<anari_vec::float4>, NUM_ATTRIBUTE_CHANNELS>
      m_uniformAttr;
  helium::ChangeObserverPtr<Array1D> m_primitiveId;
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Geometry *, ANARI_GEOMETRY);
