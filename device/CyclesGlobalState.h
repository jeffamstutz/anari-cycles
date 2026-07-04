// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "FrameOutputDriver.h"
// helium
#include "helium/BaseGlobalDeviceState.h"
// cycles
#include "scene/shader_nodes.h"
#include "session/session.h"
// std
#include <atomic>
#include <thread>

namespace ccl {
struct BackgroundNode;
} // namespace ccl

namespace anari_cycles {

struct CyclesGlobalState : public helium::BaseGlobalDeviceState
{
  struct ObjectUpdates
  {
    helium::TimeStamp lastSceneChange{0};
    helium::TimeStamp lastAccumulationReset{0};
  } objectUpdates;

  ccl::SessionParams session_params;
  std::unique_ptr<ccl::Session> session;
  size_t sessionSamples{0};

  ccl::Scene *scene{nullptr};
  ccl::SceneParams scene_params;
  ccl::BufferParams buffer_params;

  FrameOutputDriver *output_driver{nullptr};

  ccl::ColorNode *backgroundColor{nullptr};
  ccl::ColorNode *ambientColor{nullptr};
  ccl::ValueNode *ambientIntensity{nullptr};

  // Scene mutation guard //

  // The Cycles render session thread reads/updates the scene while holding
  // scene->mutex (Session::run_update_for_next_iteration()). Waiting on the
  // output driver only guarantees the final tile of a frame was written -- the
  // render thread performs at least one more scene-update pass afterwards and
  // stays alive between frames. So *any* mutation of the ccl::Scene made from
  // an ANARI API thread (commit-buffer flushes, world/camera/renderer sync,
  // scene-node creation/deletion in object ctors/dtors) must hold the same
  // mutex. SceneLock is reentrant per thread because mutations nest (e.g. an
  // object destructor running inside a commit-buffer flush).
  //
  // Lock-ordering rules to stay deadlock free:
  //   - SceneLock is always the innermost lock; never acquire helium object
  //     locks while holding it.
  //   - Never hold SceneLock across anything that waits on the render thread
  //     (output_driver->wait(), session->wait()).
  struct SceneLock
  {
    SceneLock(CyclesGlobalState &s);
    ~SceneLock();
    SceneLock(const SceneLock &) = delete;
    SceneLock &operator=(const SceneLock &) = delete;

   private:
    CyclesGlobalState &m_state;
    bool m_engaged{false};
  };

  std::atomic<std::thread::id> sceneLockOwner{std::thread::id()};
  int sceneLockDepth{0}; // only accessed by the lock-owning thread

  // Deferred deletion of Cycles geometry nodes //

  // Deleting a ccl::Geometry (incl. ccl::Light) immediately frees it while
  // scene->objects -- only rebuilt on the next world sync -- may still
  // reference it. Even with SceneLock held, the scene would be inconsistent
  // once the lock is released, and the render thread's next update pass would
  // walk dangling Object::geometry pointers. Instead, nodes are 'retired'
  // (kept alive in the scene) and only truly deleted right after
  // scene->objects has been rebuilt without them. Nodes never purged (e.g.
  // device torn down before another frame renders) are freed by the
  // ccl::Scene destructor. Both methods take SceneLock themselves
  // (reentrant, so callers already holding it pay nothing).
  std::vector<ccl::Geometry *> retiredGeometry;
  void retireGeometry(ccl::Geometry *g);
  void purgeRetiredGeometry(); // only right after rebuilding scene->objects

  // Motion blur //

  // Motion blur is pay-for-what-you-use: the integrator's motion_blur flag
  // is the OR of these two, maintained by World::setCyclesWorldObjects()
  // (instance motion baked into scene objects) and Camera::setCameraCurrent()
  // (camera motion). Both call syncIntegratorMotionBlur() (under SceneLock)
  // after updating their flag; scenes without motion keep the integrator
  // flag off and render exactly as before.
  bool objectsHaveMotion{false};
  bool cameraHasMotion{false};
  void syncIntegratorMotionBlur();

  // Helper methods //

  CyclesGlobalState(ANARIDevice d);
  void waitOnCurrentFrame() const;
};

#define CYCLES_ANARI_TYPEFOR_SPECIALIZATION(type, anari_type)                  \
  namespace anari {                                                            \
  ANARI_TYPEFOR_SPECIALIZATION(type, anari_type);                              \
  }

#define CYCLES_ANARI_TYPEFOR_DEFINITION(type)                                  \
  namespace anari {                                                            \
  ANARI_TYPEFOR_DEFINITION(type);                                              \
  }

} // namespace anari_cycles
