// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Geometry.h"
#include "GeometryAttributes.h"
// subtypes
#include "Curve.h"
#include "Isosurface.h"
#include "Mesh.h"
#include "Sphere.h"
#include "Tube.h"
#include "UnknownGeometry.h"
// std
#include <algorithm>
#include <string>

namespace anari_cycles {

Geometry::Geometry(CyclesGlobalState *s)
    : Object(ANARI_GEOMETRY, s),
      m_vertexAttr{{{this}, {this}, {this}, {this}, {this}}},
      m_primitiveAttr{{{this}, {this}, {this}, {this}, {this}}},
      m_primitiveId(this)
{}

Geometry::~Geometry() = default;

Geometry *Geometry::createInstance(std::string_view type, CyclesGlobalState *s)
{
  if (type == "triangle")
    return new Mesh(s, false, "triangle");
  else if (type == "quad")
    return new Mesh(s, true, "quad");
  else if (type == "sphere")
    return new Sphere(s);
  else if (type == "curve")
    return new Curve(s);
  else if (type == "cylinder")
    return new Tube(s, Tube::RadiusSource::PER_PRIMITIVE, "cylinder");
  else if (type == "cone")
    return new Tube(s, Tube::RadiusSource::PER_VERTEX, "cone");
  else if (type == "isosurface")
    return new Isosurface(s);
  else
    return new UnknownGeometry(type, s);
}

void Geometry::finalize()
{
  Object::finalize();
}

bool Geometry::hasDeformationMotion() const
{
  return false;
}

bool Geometry::bakeDeformationMotion(ccl::Geometry *, const helium::box1 &) const
{
  return false;
}

void Geometry::commitAttributeParameters()
{
  for (int c = 0; c < NUM_ATTRIBUTE_CHANNELS; c++) {
    const std::string suffix = CHANNEL_PARAM[c];
    m_uniformAttr[c].reset();
    anari_vec::float4 v = {0.f, 0.f, 0.f, 1.f};
    if (getParam(suffix, ANARI_FLOAT32_VEC4, &v))
      m_uniformAttr[c] = v;
    m_vertexAttr[c] = getParamObject<Array1D>("vertex." + suffix);
    m_primitiveAttr[c] = getParamObject<Array1D>("primitive." + suffix);
  }

  m_primitiveId = getParamObject<Array1D>("primitive.id");
  if (m_primitiveId && m_primitiveId->elementType() != ANARI_UINT32
      && m_primitiveId->elementType() != ANARI_UINT64) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'primitive.id' must be an array of UINT32 or UINT64 (got %s) "
        "-- ignoring",
        anari::toString(m_primitiveId->elementType()));
    m_primitiveId = nullptr;
  }
}

bool Geometry::hasPerPrimitiveAttributes() const
{
  if (m_primitiveId)
    return true;
  return std::any_of(m_primitiveAttr.begin(),
      m_primitiveAttr.end(),
      [](const auto &a) { return bool(a); });
}

helium::IntrusivePtr<Array1D> Geometry::validatedVertexPosition(
    const char *subtype)
{
  auto array = getParamObject<Array1D>("vertex.position");
  if (array && array->elementType() != ANARI_FLOAT32_VEC3) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'vertex.position' on %s geometry must be an array of FLOAT32_VEC3 "
        "(got %s) -- ignoring",
        subtype,
        anari::toString(array->elementType()));
    return {};
  }
  return array;
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Geometry *);
