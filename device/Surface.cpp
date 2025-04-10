// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Surface.h"

namespace cycles {

Surface::Surface(CyclesGlobalState *s)
    : Object(ANARI_SURFACE, s), m_geometry(this)
{}

Surface::~Surface()
{
  cleanupCyclesNode();
}

void Surface::commitParameters()
{
  auto *prevGeometry = m_geometry.get();
  auto *prevMaterial = m_material.ptr;
  m_geometry = getParamObject<Geometry>("geometry");
  m_material = getParamObject<Material>("material");
  m_geometryHandleChanged = prevGeometry != m_geometry.get();
  m_materialHandleChanged = prevMaterial != m_material.ptr;
}

void Surface::finalize()
{
  if (m_geometryHandleChanged) {
    cleanupCyclesNode();
    if (m_geometry)
      m_cyclesGeometryNode = m_geometry->createCyclesGeometryNode();
  }

  if (isValid()) {
    m_geometry->syncCyclesNode(m_cyclesGeometryNode);
    if (m_materialHandleChanged || m_geometryHandleChanged) {
      ccl::array<ccl::Node *> used_shaders;
      used_shaders.push_back_slow(m_material->cyclesShader());
      m_cyclesGeometryNode->set_used_shaders(used_shaders);
    }
    m_cyclesGeometryNode->tag_update(deviceState()->scene, true);
  }

  m_geometryHandleChanged = false;
  m_materialHandleChanged = false;
}

const Geometry *Surface::geometry() const
{
  return m_geometry.get();
}

const Material *Surface::material() const
{
  return m_material.ptr;
}

ccl::Geometry *Surface::cyclesGeometry() const
{
  return m_cyclesGeometryNode;
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

void Surface::cleanupCyclesNode()
{
  auto &state = *deviceState();
  if (auto *cg = cyclesGeometry(); cg != nullptr)
    state.scene->delete_node(cg);
  m_cyclesGeometryNode = nullptr;
}

} // namespace cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(cycles::Surface *);
