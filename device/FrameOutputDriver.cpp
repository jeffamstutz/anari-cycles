// Copyright 2022 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "FrameOutputDriver.h"
#include "Frame.h"
// std
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

#include "util/tbb.h"

namespace anari_cycles {

struct FrameOutputDriver::Impl
{
  helium::IntrusivePtr<Frame> frame;
  std::vector<float4> buffer;
  bool renderFinished{true};
  std::mutex mutex;
  std::condition_variable cv;

  std::chrono::time_point<std::chrono::steady_clock> start;
};

FrameOutputDriver::FrameOutputDriver()
{
  m_impl = std::make_shared<Impl>();
}

void FrameOutputDriver::write_render_tile(const Tile &tile)
{
  auto &frame = *m_impl->frame;
  auto &frameData = frame.m_frameData;

  if (!(tile.size == tile.full_size)) {
    frame.reportMessage(ANARI_SEVERITY_WARNING, "rejecting partial tile");
    renderEnd();
    return;
  }

  const int width = tile.size.x;
  const int height = tile.size.y;

#if 0
  frame.reportMessage(
      ANARI_SEVERITY_DEBUG, "receiving %i x %i frame", width, height);
#endif

  if (frameData.size.x != width || frameData.size.y != height) {
    frame.reportMessage(ANARI_SEVERITY_WARNING,
        "rejecting frame -- buffer size mismatch,"
        " got {%i, %i} but target is {%i, %i}",
        width,
        height,
        frameData.size.x,
        frameData.size.y);
    renderEnd();
    return;
  }

  extractColorPass(tile);
  extractDepthPass(tile);
  renderEnd();
}

bool FrameOutputDriver::renderBegin(Frame *f)
{
  std::lock_guard<std::mutex> lock(m_impl->mutex);
  m_impl->start = std::chrono::steady_clock::now();
  bool newFrame = m_impl->frame.ptr == f;
  m_impl->frame = f;
  m_impl->renderFinished = false;
  return newFrame;
}

void FrameOutputDriver::renderEnd()
{
  std::lock_guard<std::mutex> lock(m_impl->mutex);

  auto end = std::chrono::steady_clock::now();
  m_impl->frame->m_duration =
      std::chrono::duration<float>(end - m_impl->start).count();

  m_impl->frame = nullptr;
  m_impl->renderFinished = true;

  // Notify the wait thread
  m_impl->cv.notify_one();
}

void FrameOutputDriver::wait()
{
  if (!ready()) {
    std::unique_lock<std::mutex> lock(m_impl->mutex);
    m_impl->cv.wait(lock, [this] { return ready(); });
  }
}

bool FrameOutputDriver::ready() const
{
  return m_impl->renderFinished;
}

void FrameOutputDriver::extractColorPass(const Tile &tile)
{
  const auto format = m_impl->frame->m_colorType;
  if (format == ANARI_UNKNOWN)
    return;

  const int width = tile.size.x;
  const int height = tile.size.y;

  const bool isFloat = format == ANARI_FLOAT32_VEC4;

  if (!isFloat)
    m_impl->buffer.resize(width * height);

  float *dst = isFloat ? (float *)m_impl->frame->m_pixelBuffer.data()
                       : (float *)m_impl->buffer.data();
  if (!tile.get_pass_pixels("combined", 4, dst))
    m_impl->frame->reportMessage(
        ANARI_SEVERITY_ERROR, "Failed to read 'combined' pass");

  if (!isFloat) {
    auto *transformDst = (uint32_t *)m_impl->frame->m_pixelBuffer.data();
    parallel_for(size_t(0), m_impl->buffer.size(), [&](size_t i) {
      helium::float4 v;
      std::memcpy(&v, &m_impl->buffer[i], sizeof(v));
      transformDst[i] = format == ANARI_UFIXED8_VEC4
          ? helium::cvt_color_to_uint32(v)
          : helium::cvt_color_to_uint32_srgb(v);
    });
  }
}

void FrameOutputDriver::extractDepthPass(const Tile &tile)
{
  if (m_impl->frame->m_depthType != ANARI_FLOAT32)
    return;

  if (!tile.get_pass_pixels("depth", 1, m_impl->frame->m_depthBuffer.data()))
    m_impl->frame->reportMessage(
        ANARI_SEVERITY_ERROR, "Failed to read 'depth' pass");
}

} // namespace anari_cycles
