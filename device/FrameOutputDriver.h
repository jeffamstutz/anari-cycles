// Copyright 2022 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "cycles_math.h"
// cycles
#include "session/output_driver.h"
// std
#include <memory>
#include <vector>

namespace anari_cycles {

struct Frame;

struct FrameOutputDriver : public ccl::OutputDriver
{
  FrameOutputDriver();
  ~FrameOutputDriver() override;

  void write_render_tile(const Tile &tile) override;

  bool renderBegin(Frame *);
  void renderEnd();

  // Blocks until the in-flight render (if any) has delivered its tile.
  void wait();
  // wait() plus: blocks until any queued/running frame-completion callback
  // has returned (KHR_FRAME_COMPLETION_CALLBACK requires this before
  // anariFrameReady(ANARI_WAIT) may return). No-ops the callback-drain part
  // when called from the callback thread itself, so a callback re-rendering
  // synchronously does not deadlock on its own completion.
  void waitForCallbacks();
  bool ready() const;

  // Stop the completion-callback thread and drop (never invoke) any queued
  // callbacks; called during device teardown while everything is alive.
  void shutdownCallbackThread();

 private:
  void extractColorPass(const Tile &tile);
  void extractDepthPass(const Tile &tile);
  void extractNormalPass(const Tile &tile);
  void extractAlbedoPass(const Tile &tile);
  void extractObjectIdPass(const Tile &tile);
  void extractAovIdPass(
      const Tile &tile, const char *passName, std::vector<uint32_t> &dst);
  void extractLightgroupPasses(const Tile &tile);
  void extractAuxPasses(const Tile &tile);

  // KHR_FRAME_COMPLETION_CALLBACK support; see the threading notes in
  // FrameOutputDriver.cpp.
  void callbackThreadLoop();

  struct Impl;
  std::shared_ptr<Impl> m_impl;
};

} // namespace anari_cycles
