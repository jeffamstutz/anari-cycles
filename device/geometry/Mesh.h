// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"
#include "array/ObjectArray.h"
// cycles
#include "scene/mesh.h"
// std
#include <string>
#include <vector>

namespace anari_cycles {

// Both subtypes become a Cycles triangle mesh; quads split into two triangles
// each: (v0,v1,v2) and (v0,v2,v3). Per-primitive attributes replicate across
// both split triangles, faceVarying values map through the split corners.
//
// KHR_GEOMETRY_TRIANGLE/QUAD_MOTION_DEFORMATION: 'vertex.position' (and
// optionally 'vertex.normal'/'vertex.tangent') may be an ARRAY1D of ARRAY1D
// handles -- per-key vertex arrays uniformly distributed over the 'time'
// interval (FLOAT32_BOX1, default [0,1]). The first position key acts as the
// static representative array (primitive count, attribute sizing); the
// per-shutter Cycles motion steps are baked by bakeDeformationMotion() during
// world rebuilds (see Surface::bakeGeometryMotion()). Tangents do not motion
// blur in Cycles, so a nested 'vertex.tangent' contributes its middle key as
// the static tangent. Deformation keys are ignored while 'subdivision' is
// active (Cycles' tessellator does not re-dice motion steps).
//
// CYCLES_GEOMETRY_SUBDIVISION: when 'subdivision' is "linear" or
// "catmullClark" the primitives are fed to Cycles as subdivision base faces
// (the 'subd_faces' path) instead of plain triangles; Cycles then dices them
// into micro-triangles at scene-update time (adaptive, object-space metric:
// patch edges split until shorter than 'subdivisionDicingRate' object-space
// units, capped at 2^'subdivisionLevel' segments per edge). Attributes are
// uploaded to the separate 'subd_attributes' set and interpolated onto the
// diced mesh by the tessellator. Catmull-Clark evaluates the limit surface
// through OpenSubdiv (build requirement; Cycles silently dices linearly
// without it) and computes smooth normals itself, so user-supplied
// normals/tangents are ignored while subdivision is active.
struct Mesh : public Geometry
{
  Mesh(CyclesGlobalState *s, bool quads, const char *subtype);
  ~Mesh() override;

  void commitParameters() override;
  void finalize() override;

  ccl::Geometry *createCyclesGeometryNode() override;
  void syncCyclesNode(ccl::Geometry *node) const override;

  bool hasDeformationMotion() const override;
  bool bakeDeformationMotion(
      ccl::Geometry *node, const helium::box1 &shutter) const override;

  box3 bounds() const override;

 private:
  size_t numPrims() const; // ANARI primitives (triangles or quads)
  size_t numTriangles() const
  {
    return numPrims() * (m_quads ? 2 : 1);
  }
  // Index into a faceVarying array for corner 'corner' of the triangulated
  // mesh (3 corners per triangle; quads have 4 faceVarying values per
  // primitive that map to split-triangle corners {0,1,2} and {0,2,3}).
  size_t fvIndex(size_t corner) const;

  void setVertexPosition(ccl::Mesh *mesh) const;
  void setPrimitiveIndex(ccl::Mesh *mesh) const;
  void setAttributes(ccl::Mesh *mesh) const;
  void setNormals(ccl::Mesh *mesh) const;
  void setTangents(ccl::Mesh *mesh) const;

  // CYCLES_GEOMETRY_SUBDIVISION (see struct comment)
  bool subdivisionEnabled() const
  {
    return m_subdivisionType != ccl::Mesh::SUBDIVISION_NONE;
  }
  void syncSubdCyclesNode(ccl::Mesh *mesh) const;
  void setSubdFaces(ccl::Mesh *mesh) const;
  void setSubdCreases(ccl::Mesh *mesh) const;
  void setSubdAttributes(ccl::Mesh *mesh) const;
  static void clearSubdivisionState(ccl::Mesh *mesh);

  // KHR_GEOMETRY_*_MOTION_DEFORMATION (see struct comment)
  using KeyVector = std::vector<helium::ChangeObserverPtr<Array1D>>;
  // Validated inner key arrays of a nested vertex parameter; invalid entries
  // (wrong handle/element type or -- with 'requireEqualSizes' -- a size other
  // than the first valid key's) are skipped with a warning.
  template <typename ValidFn>
  void readVertexKeys(const ObjectArray *data,
      const char *param,
      bool requireEqualSizes,
      ValidFn &&valid,
      KeyVector &out);
  // Interpolated vertex positions at absolute frame time 't' (linear between
  // the bracketing keys, clamped outside 'time').
  void samplePositionKeys(float t, ccl::float3 *dst) const;
  // Interpolated (renormalized) vertex normals at frame time 't', from
  // per-key float4 conversions computed once per bake (normal keys may be
  // FIXED16, so they cannot be read in place like position keys).
  using ConvertedKeys = std::vector<std::vector<anari_vec::float4>>;
  void sampleNormalKeys(const ConvertedKeys &keys,
      float t,
      ccl::packed_normal *dst,
      size_t count) const;
  static void clearDeformationMotionState(ccl::Mesh *mesh);

  helium::ChangeObserverPtr<Array1D> m_index;
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  helium::ChangeObserverPtr<Array1D> m_vertexNormal;
  helium::ChangeObserverPtr<Array1D> m_vertexTangent;
  // KHR_GEOMETRY_*_MOTION_DEFORMATION: the nested (array-of-arrays) forms of
  // the vertex parameters and their validated key arrays. All are observed so
  // data changes on the outer array or any key re-finalize this geometry.
  helium::ChangeObserverPtr<ObjectArray> m_positionKeyData;
  helium::ChangeObserverPtr<ObjectArray> m_normalKeyData;
  helium::ChangeObserverPtr<ObjectArray> m_tangentKeyData;
  KeyVector m_positionKeys;
  KeyVector m_normalKeys;
  KeyVector m_tangentKeys;
  helium::box1 m_motionTime{0.f, 1.f};
  std::array<helium::ChangeObserverPtr<Array1D>, NUM_ATTRIBUTE_CHANNELS>
      m_faceVaryingAttr;
  helium::ChangeObserverPtr<Array1D> m_faceVaryingNormal;
  helium::ChangeObserverPtr<Array1D> m_faceVaryingTangent;
  helium::ChangeObserverPtr<Array1D> m_creaseIndex;
  helium::ChangeObserverPtr<Array1D> m_creaseWeight;
  ccl::Mesh::SubdivisionType m_subdivisionType{ccl::Mesh::SUBDIVISION_NONE};
  int m_subdivisionLevel{12};
  float m_subdivisionDicingRate{1.f};
  bool m_quads{false};
  const char *m_subtype{"triangle"};
};

} // namespace anari_cycles
