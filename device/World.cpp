// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "World.h"
// std
#include <algorithm>
// cycles
#include "scene/background.h"
#include "scene/devicescene.h"
#include "scene/object.h"

namespace anari_cycles {

World::World(CyclesGlobalState *s)
    : Object(ANARI_WORLD, s),
      m_zeroSurfaceData(this),
      m_zeroLightData(this),
      m_zeroVolumeData(this),
      m_instanceData(this)
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

void World::setCyclesWorldObjects(const helium::box1 &shutter)
{
  auto &state = *deviceState();
  auto *scene = state.scene;

  // Self-guarding against the render thread (reentrant when the caller --
  // normally Frame::renderFrame() -- already holds the lock).
  CyclesGlobalState::SceneLock sceneLock(state);

  // Remove the old objects through Scene::delete_nodes() rather than
  // objects.clear(): managers cache per-object state keyed by Object pointer
  // (e.g. VolumeManager::object_octrees_) and only delete_nodes() tells them
  // to drop those entries.
  if (!scene->objects.empty()) {
    ccl::set<ccl::Object *> oldObjects;
    for (size_t i = 0; i < scene->objects.size(); i++)
      oldObjects.insert(scene->objects[i]);
    scene->delete_nodes(oldObjects);
  }

  bool objectsHaveMotion = false;

  objectsHaveMotion |= m_zeroInstance->addInstanceObjectsToCyclesScene(shutter);

  if (m_instanceData) {
    auto **instancesBegin = (Instance **)m_instanceData->handlesBegin();
    auto **instancesEnd = (Instance **)m_instanceData->handlesEnd();
    std::for_each(instancesBegin, instancesEnd, [&](Instance *i) {
      if (!i->isValid()) {
        i->warnIfUnknownObject();
        return;
      }
      objectsHaveMotion |= i->addInstanceObjectsToCyclesScene(shutter);
    });
  }

  state.objectsHaveMotion = objectsHaveMotion;
  state.syncIntegratorMotionBlur();
  m_bakedShutter = shutter;

  // Handle HDRI light management after objects are set up
  setupHDRIBackground();

  scene->object_manager->tag_update(scene, ObjectManager::UPDATE_ALL);
  scene->geometry_manager->tag_update(scene, GeometryManager::UPDATE_ALL);
  scene->light_manager->tag_update(scene, ccl::LightManager::UPDATE_ALL);
  scene->shader_manager->tag_update(scene, ShaderManager::UPDATE_ALL);
}

bool World::motionRequiresRebake(const helium::box1 &shutter)
{
  const bool shutterChanged = shutter.lower != m_bakedShutter.lower
      || shutter.upper != m_bakedShutter.upper;
  if (!shutterChanged)
    return false;
  if (hasMotionInstances())
    return true;
  // Without motion instances the baked objects do not depend on the shutter;
  // record it so static worlds don't rescan their instances every frame.
  // NOTE: multiple frames rendering this world with cameras whose shutters
  // differ will rebake on every alternation when motion instances exist --
  // correct, but pathological for multi-view apps.
  m_bakedShutter = shutter;
  return false;
}

bool World::hasMotionInstances() const
{
  if (!m_instanceData)
    return false;
  auto **instancesBegin = (Instance **)m_instanceData->handlesBegin();
  auto **instancesEnd = (Instance **)m_instanceData->handlesEnd();
  return std::any_of(instancesBegin, instancesEnd, [](const Instance *i) {
    return i->isValid() && i->hasMotion();
  });
}

Light *World::findFirstHDRILight() const
{
  // Check lights in the zero instance
  if (m_zeroLightData) {
    auto **lightsBegin = (Light **)m_zeroLightData->handlesBegin();
    auto **lightsEnd = (Light **)m_zeroLightData->handlesEnd();

    for (Light **lightPtr = lightsBegin; lightPtr != lightsEnd; ++lightPtr) {
      if (Light *light = *lightPtr; light && light->cyclesLight()) {
        // Check if this is an HDRI light - we'll do this by checking the cycles
        // light type (unknown light subtypes have no Cycles light at all)
        if (light->cyclesLight()->get_light_type() == ccl::LIGHT_BACKGROUND) {
          return light;
        }
      }
    }
  }

  // Check lights in instances
  if (m_instanceData) {
    auto **instancesBegin = (Instance **)m_instanceData->handlesBegin();
    auto **instancesEnd = (Instance **)m_instanceData->handlesEnd();

    for (auto **instPtr = instancesBegin; instPtr != instancesEnd; ++instPtr) {
      Instance *instance = *instPtr;
      if (instance && instance->isValid() && instance->group()) {
        auto *group = instance->group();
        if (group->lightData()) {
          auto **lightsBegin = (Light **)group->lightData()->handlesBegin();
          auto **lightsEnd = (Light **)group->lightData()->handlesEnd();

          for (Light **lightPtr = lightsBegin; lightPtr != lightsEnd;
              ++lightPtr) {
            if (Light *light = *lightPtr; light && light->cyclesLight()) {
              // Check if this is an HDRI light - we'll do this by checking the
              // cycles light type (unknown subtypes have no Cycles light)
              if (light->cyclesLight()->get_light_type()
                  == ccl::LIGHT_BACKGROUND) {
                return light;
              }
            }
          }
        }
      }
    }
  }

  return nullptr;
}

void World::setupHDRIBackground()
{
  // Find first HDRI light from the world (cached so per-frame consumers do
  // not re-walk every instance's light array; see backgroundHdriLight()).
  Light *hdriLight = findFirstHDRILight();
  m_backgroundHdriLight = hdriLight;
  if (hdriLight) {
    // Set the new HDRI background
    deviceState()->scene->background->set_shader(hdriLight->cyclesShader());
    deviceState()->scene->background->tag_update(deviceState()->scene);
  } else {
    // Clear any existing HDRI background first
    deviceState()->scene->background->set_shader(nullptr);
  }
}

Light *World::backgroundHdriLight() const
{
  return m_backgroundHdriLight.ptr;
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
