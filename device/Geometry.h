// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Array.h"
#include "Object.h"
// cycles
#include "scene/geometry.h"
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

  std::array<helium::IntrusivePtr<Array1D>, NUM_ATTRIBUTE_CHANNELS>
      m_vertexAttr;
  std::array<helium::IntrusivePtr<Array1D>, NUM_ATTRIBUTE_CHANNELS>
      m_primitiveAttr;
  std::array<std::optional<anari_vec::float4>, NUM_ATTRIBUTE_CHANNELS>
      m_uniformAttr;
  helium::IntrusivePtr<Array1D> m_primitiveId;
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Geometry *, ANARI_GEOMETRY);
