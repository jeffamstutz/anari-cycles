// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "CyclesGlobalState.h"
#include "Frame.h"
// cycles
#include "scene/geometry.h"
#include "scene/integrator.h"
#include "scene/light.h"
#include "scene/object.h"
#include "scene/scene.h"

namespace anari_cycles {

CyclesGlobalState::CyclesGlobalState(ANARIDevice d)
    : helium::BaseGlobalDeviceState(d)
{}

void CyclesGlobalState::waitOnCurrentFrame() const
{
  if (output_driver)
    output_driver->wait();
}

CyclesGlobalState::SceneLock::SceneLock(CyclesGlobalState &s) : m_state(s)
{
  if (!m_state.scene)
    return; // device not yet initialized -- no render thread to race with

  m_engaged = true;

  const auto thisThread = std::this_thread::get_id();
  if (m_state.sceneLockOwner.load(std::memory_order_acquire) == thisThread) {
    m_state.sceneLockDepth++; // reentrant acquire on the owning thread
  } else {
    m_state.scene->mutex.lock();
    m_state.sceneLockOwner.store(thisThread, std::memory_order_release);
    m_state.sceneLockDepth = 1;
  }
}

CyclesGlobalState::SceneLock::~SceneLock()
{
  if (!m_engaged)
    return;

  if (--m_state.sceneLockDepth == 0) {
    m_state.sceneLockOwner.store(std::thread::id(), std::memory_order_release);
    m_state.scene->mutex.unlock();
  }
}

void CyclesGlobalState::syncIntegratorMotionBlur()
{
  const bool motionBlur = objectsHaveMotion || cameraHasMotion;
  if (scene->integrator->get_motion_blur() == motionBlur)
    return;

  scene->integrator->set_motion_blur(motionBlur);
  // Flipping motion_blur changes Scene::need_motion(), which alters how
  // objects (motion decomposition, BVH time steps) and geometry sync to the
  // device -- retag them so a toggle without a world rebuild (e.g. camera
  // motion added to an untouched world) still resyncs everything.
  scene->object_manager->tag_update(scene, ccl::ObjectManager::UPDATE_ALL);
  scene->geometry_manager->tag_update(scene, ccl::GeometryManager::UPDATE_ALL);
}

void CyclesGlobalState::retireGeometry(ccl::Geometry *g)
{
  if (!g)
    return;
  SceneLock lock(*this); // self-guarding; reentrant when caller holds it
  retiredGeometry.push_back(g);
  // Force a world-object rebuild on the next frame so scene->objects drops
  // its references to the retired node before it is purged.
  objectUpdates.lastSceneChange = helium::newTimeStamp();
}

void CyclesGlobalState::purgeRetiredGeometry()
{
  if (retiredGeometry.empty())
    return;
  SceneLock lock(*this); // self-guarding; reentrant when caller holds it
  // Batch deletion via Scene::delete_nodes() so managers that cache state
  // keyed by node pointers (e.g. VolumeManager octrees) evict their entries.
  const ccl::set<ccl::Geometry *> nodes(
      retiredGeometry.begin(), retiredGeometry.end());
  scene->delete_nodes(nodes);
  retiredGeometry.clear();
}

} // namespace anari_cycles
