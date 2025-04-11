// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "FrameOutputDriver.h"
// helium
#include "helium/BaseGlobalDeviceState.h"
// cycles
#include "session/session.h"
// std
#include <atomic>

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

  ccl::BackgroundNode *background{nullptr};
  ccl::BackgroundNode *ambient{nullptr};

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
