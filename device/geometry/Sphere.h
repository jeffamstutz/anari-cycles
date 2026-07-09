// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"

namespace anari_cycles {

struct Sphere : public Geometry
{
  Sphere(CyclesGlobalState *s);
  ~Sphere() override;

  void commitParameters() override;
  void finalize() override;

  ccl::Geometry *createCyclesGeometryNode() override;
  void syncCyclesNode(ccl::Geometry *node) const override;

  box3 bounds() const override;

 private:
  void setSpheres(ccl::PointCloud *pc) const;
  void setAttributes(ccl::PointCloud *pc) const;

  // Maps the i'th sphere to its 'vertex.*' array index through the optional
  // 'primitive.index' array (identity when absent). Shared by setSpheres(),
  // setAttributes() and bounds() so they can never disagree on the layout.
  struct VertexIndexer
  {
    const uint32_t *idx32{nullptr};
    const uint64_t *idx64{nullptr};
    size_t operator()(size_t i) const
    {
      return idx64 ? size_t(idx64[i]) : (idx32 ? size_t(idx32[i]) : i);
    }
  };
  VertexIndexer vertexIndexer() const;
  size_t numSpheres() const;

  helium::ChangeObserverPtr<Array1D> m_index;
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  helium::ChangeObserverPtr<Array1D> m_vertexRadius;
  float m_radius{1.f};
};

} // namespace anari_cycles
