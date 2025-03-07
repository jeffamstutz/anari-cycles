// Copyright 2022 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "cycles_math.h"
// cycles
#include "session/output_driver.h"
// std
#include <memory>

namespace cycles {

struct Frame;

struct FrameOutputDriver : public ccl::OutputDriver {
  FrameOutputDriver();

  void write_render_tile(const Tile &tile) override;

  bool renderBegin(Frame *);
  void renderEnd();

  void wait();
  bool ready() const;

 private:
  void extractColorPass(const Tile &tile);
  void extractDepthPass(const Tile &tile);

  struct Impl;
  std::shared_ptr<Impl> m_impl;
};

}  // namespace cycles
