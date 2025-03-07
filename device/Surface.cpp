// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Surface.h"

namespace cycles {

Surface::Surface(CyclesGlobalState *s) : Object(ANARI_SURFACE, s) {}

Surface::~Surface() {}

void Surface::commitParameters()
{
  m_geometry = getParamObject<Geometry>("geometry");
  m_material = getParamObject<Material>("material");
}

const Geometry *Surface::geometry() const
{
  return m_geometry.ptr;
}

const Material *Surface::material() const
{
  return m_material.ptr;
}

std::unique_ptr<ccl::Geometry> Surface::makeCyclesGeometry()
{
  auto g = m_geometry->makeCyclesGeometry();
  ccl::array<ccl::Node *> used_shaders;
  used_shaders.push_back_slow(m_material->cyclesShader());
  g->set_used_shaders(used_shaders);
  return g;
}

bool Surface::isValid() const
{
  return m_geometry && m_geometry->isValid() && m_material
      && m_material->isValid();
}

void Surface::warnIfUnknownObject() const
{
  if (m_geometry)
    m_geometry->warnIfUnknownObject();
  if (m_material)
    m_material->warnIfUnknownObject();
}

} // namespace cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(cycles::Surface *);
