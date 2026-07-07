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

  // Handle background (hdri/sky) light management after objects are set up
  setupBackground();

  // Scene::has_shadow_catcher() caches its object scan behind a dirty flag
  // that only Object::tag_update() raises; objects here are created directly
  // (and removed via delete_nodes()), so re-tag it whenever the object set
  // changes or a 'shadowCatcher' surface added/removed after the first
  // render would go unnoticed (stale passes/kernel features).
  scene->tag_shadow_catcher_modified();

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

Light *World::findFirstBackgroundLight(size_t &backgroundLightCount) const
{
  // Background-type lights (hdri/sky) are recognized by their Cycles light
  // type (unknown light subtypes have no Cycles light at all).
  Light *first = nullptr;
  backgroundLightCount = 0;
  auto scanLights = [&](const ObjectArray *lightData) {
    if (!lightData)
      return;
    auto **lightsBegin = (Light **)lightData->handlesBegin();
    auto **lightsEnd = (Light **)lightData->handlesEnd();
    for (Light **lightPtr = lightsBegin; lightPtr != lightsEnd; ++lightPtr) {
      if (Light *light = *lightPtr; light && light->cyclesLight()
          && light->cyclesLight()->get_light_type() == ccl::LIGHT_BACKGROUND) {
        if (!first)
          first = light;
        backgroundLightCount++;
      }
    }
  };

  // Lights in the zero instance, then in instanced groups.
  scanLights(m_zeroLightData.get());
  if (m_instanceData) {
    auto **instancesBegin = (Instance **)m_instanceData->handlesBegin();
    auto **instancesEnd = (Instance **)m_instanceData->handlesEnd();
    for (auto **instPtr = instancesBegin; instPtr != instancesEnd; ++instPtr) {
      Instance *instance = *instPtr;
      if (instance && instance->isValid() && instance->group())
        scanLights(instance->group()->lightData());
    }
  }

  return first;
}

void World::setupBackground()
{
  // Find the first background-type (hdri/sky) light in the world (cached so
  // per-frame consumers do not re-walk every instance's light array; see
  // backgroundLight()).
  size_t backgroundLightCount = 0;
  Light *bgLight = findFirstBackgroundLight(backgroundLightCount);
  if (backgroundLightCount > 1) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "world contains %zu background-type ('hdri'/'sky') lights, but "
        "Cycles has a single background slot; using the first one and "
        "ignoring the rest",
        backgroundLightCount);
  }
  m_backgroundLight = bgLight;
  auto *background = deviceState()->scene->background;
  if (bgLight) {
    // Set the new environment background
    background->set_shader(bgLight->cyclesShader());
    // CYCLES_LIGHTGROUPS: background-hit contributions of the environment go
    // to its 'lightGroup' pass (its scene object routes light-tree samples
    // there, but camera/escaped rays read Background::lightgroup instead).
    background->set_lightgroup(OIIO::ustring(bgLight->lightGroup()));
    background->tag_update(deviceState()->scene);
  } else {
    // Clear any existing environment background first
    background->set_shader(nullptr);
    background->set_lightgroup(OIIO::ustring());
  }
}

Light *World::backgroundLight() const
{
  return m_backgroundLight.ptr;
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
