// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"
#include "MarchingCubes.h"
#include "spatial_field/SpatialField.h"
// std
#include <optional>
#include <vector>

namespace anari_cycles {

// KHR_GEOMETRY_ISOSURFACE. Cycles cannot ray-trace implicit isosurfaces, so
// the shells are extracted into a triangle mesh at finalize time by marching
// cubes over the spatial field's dense voxel grid (see MarchingCubes.cpp;
// fields provide the grid via SpatialField::getDenseVoxelGrid — nanovdb
// fields densify their sparse grid). Edge-welded vertices keep each shell
// watertight, and vertex normals come from field gradients, pointing toward
// decreasing field values (out of the enclosed field > isovalue region).
//
// One ANARI primitive per isovalue: 'primitive.*' attributes and
// 'primitive.id' are indexed by the isovalue index of the shell a triangle
// belongs to (the implicit 'primitiveId' is that index). 'vertex.*'
// attribute arrays are not meaningful for extracted surfaces (the registry
// defines none) and are ignored.
//
// The field is observed for changes, so re-committing the field (or the
// array-data/isovalue-array underneath it) re-extracts and re-syncs through
// the usual observation chain; extraction is skipped when neither the field
// nor the isovalue set actually changed (e.g. attribute-only updates).
struct Isosurface : public Geometry
{
  Isosurface(CyclesGlobalState *s);
  ~Isosurface() override;

  void commitParameters() override;
  void finalize() override;

  ccl::Geometry *createCyclesGeometryNode() override;
  void syncCyclesNode(ccl::Geometry *node) const override;

  box3 bounds() const override;

 private:
  std::vector<float> isovalues() const;

  helium::ChangeObserverPtr<SpatialField> m_field;
  helium::ChangeObserverPtr<Array1D> m_isovalueArray;
  std::optional<float> m_isovalue;

  // Cached extraction and the inputs it was computed from
  MarchingCubesMesh m_mesh;
  box3 m_bounds = empty_box3();
  // (the default values match a never-extracted state: null field, stamp 0
  // and no isovalues extract an empty mesh, which m_mesh already is)
  const SpatialField *m_extractedField{nullptr};
  helium::TimeStamp m_extractedFieldStamp{0};
  std::vector<float> m_extractedIsovalues;
};

} // namespace anari_cycles
