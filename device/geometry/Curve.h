// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"
// std
#include <vector>

namespace anari_cycles {

struct Curve : public Geometry
{
  Curve(CyclesGlobalState *s);
  ~Curve() override;

  void commitParameters() override;
  void finalize() override;

  ccl::Geometry *createCyclesGeometryNode() override;
  void syncCyclesNode(ccl::Geometry *node) const override;

  box3 bounds() const override;

 private:
  // Convert ANARI 2-vertex segments to Cycles curves, merging runs of
  // consecutive segments that share a vertex into multi-key curves.
  // keyVertex[k] is the source ANARI vertex of Cycles key k, firstKey[c] the
  // first key of Cycles curve c, curvePrim[c] the ANARI segment (primitive)
  // index curve c starts at. Merging is disabled whenever per-primitive
  // attribute sources are present (a merged curve stores only one
  // ATTR_ELEMENT_CURVE value); the implicit 'primitiveId' of a merged curve
  // is therefore approximated by its first segment's index.
  void computeCurveLayout(std::vector<int> &firstKey,
      std::vector<uint32_t> &keyVertex,
      std::vector<uint32_t> &curvePrim) const;
  void setCurves(ccl::Hair *hair,
      const std::vector<int> &firstKey,
      const std::vector<uint32_t> &keyVertex) const;
  void setAttributes(ccl::Hair *hair,
      const std::vector<uint32_t> &keyVertex,
      const std::vector<uint32_t> &curvePrim) const;

  helium::ChangeObserverPtr<Array1D> m_index;
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  helium::ChangeObserverPtr<Array1D> m_vertexRadius;
  float m_radius{1.f};
};

} // namespace anari_cycles
