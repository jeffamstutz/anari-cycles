// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "World.h"
// std
#include <algorithm>
// cycles
#include "scene/object.h"

namespace anari_cycles {

World::World(CyclesGlobalState *s)
    : Object(ANARI_WORLD, s),
      m_zeroSurfaceData(this),
      m_zeroLightData(this),
      m_zeroVolumeData(this)
{
  m_zeroGroup = new Group(s);
  m_zeroInstance = new Instance(s);
  m_zeroInstance->setParamDirect("group", m_zeroGroup.ptr);

  // never any public ref to these objects
  m_zeroGroup->refDec(helium::RefType::PUBLIC);
  m_zeroInstance->refDec(helium::RefType::PUBLIC);

  m_zeroGroup->commitParameters();
  m_zeroGroup->finalize();
  m_zeroInstance->commitParameters();
  m_zeroInstance->finalize();
}

World::~World() = default;

bool World::getProperty(const std::string_view &name,
    ANARIDataType type,
    void *ptr,
    uint64_t size,
    uint32_t flags)
{
  if (name == "bounds" && type == ANARI_FLOAT32_BOX3) {
    auto b = bounds();
    anari_vec::float3 r[2];
    r[0] = {b.lower.x, b.lower.y, b.lower.z};
    r[1] = {b.upper.x, b.upper.y, b.upper.z};
    std::memcpy(ptr, &r[0], anari::sizeOf(type));
    return true;
  }

  return Object::getProperty(name, type, ptr, size, flags);
}

void World::commitParameters()
{
  m_zeroSurfaceData = getParamObject<ObjectArray>("surface");
  m_zeroLightData = getParamObject<ObjectArray>("light");
  m_zeroVolumeData = getParamObject<ObjectArray>("volume");
  m_instanceData = getParamObject<ObjectArray>("instance");
}

void World::finalize()
{
  if (m_zeroSurfaceData) {
    reportMessage(ANARI_SEVERITY_DEBUG,
        "anari_cycles::World found surfaces in zero instance");
    m_zeroGroup->setParamDirect("surface", getParamDirect("surface"));
  } else
    m_zeroGroup->removeParam("surface");

  if (m_zeroLightData) {
    reportMessage(ANARI_SEVERITY_DEBUG,
        "anari_cycles::World found lights in zero instance");
    m_zeroGroup->setParamDirect("light", getParamDirect("light"));
  } else
    m_zeroGroup->removeParam("light");

  if (m_zeroVolumeData) {
    reportMessage(ANARI_SEVERITY_DEBUG,
        "anari_cycles::World found volumes in zero instance");
    m_zeroGroup->setParamDirect("volume", getParamDirect("volume"));
  } else
    m_zeroGroup->removeParam("volume");

  m_zeroGroup->commitParameters();
  m_zeroGroup->finalize();

  Object::finalize();
}

void World::setCyclesWorldObjects()
{
  auto &state = *deviceState();
  auto *scene = state.scene;

  scene->objects.clear();

  m_zeroInstance->addInstanceObjectsToCyclesScene();

  if (m_instanceData) {
    auto **instancesBegin = (Instance **)m_instanceData->handlesBegin();
    auto **instancesEnd = (Instance **)m_instanceData->handlesEnd();
    std::for_each(instancesBegin, instancesEnd, [](Instance *i) {
      i->addInstanceObjectsToCyclesScene();
    });
  }

  scene->object_manager->tag_update(scene, ObjectManager::UPDATE_ALL);
  scene->geometry_manager->tag_update(scene, GeometryManager::UPDATE_ALL);
  scene->light_manager->tag_update(scene, LightManager::UPDATE_ALL);
}

box3 World::bounds() const
{
  box3 b = empty_box3();

  if (m_zeroSurfaceData || m_zeroVolumeData)
    extend(b, m_zeroInstance->bounds());

  if (m_instanceData) {
    auto **instancesBegin = (Instance **)m_instanceData->handlesBegin();
    auto **instancesEnd = (Instance **)m_instanceData->handlesEnd();

    std::for_each(instancesBegin, instancesEnd, [&](Instance *i) {
      extend(b, i->bounds());
    });
  }

  return b;
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::World *);
