// Copyright 2022 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "FrameOutputDriver.h"
#include "Frame.h"
// helium
#include "helium/helium_math.h"
// std
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "util/tbb.h"

namespace anari_cycles {

// KHR_FRAME_COMPLETION_CALLBACK threading //////////////////////////////////
//
// Completion callbacks are *not* invoked on the Cycles render (session)
// thread that delivers the final tile: the common use of the callback is to
// immediately kick off the next frame (anariRenderFrame -> session->start()
// / session->wait()), which deadlocks when issued from inside the session
// thread's own main loop. Instead renderEnd() queues an invocation and a
// dedicated callback thread (owned by this driver) runs it, so the callback
// may safely call any ANARI API -- no SceneLock or driver mutex is held
// while it runs.
//
// Guarantees:
//  - waitForCallbacks() (backing anariFrameReady(ANARI_WAIT), see
//    CyclesDevice::frameReady()) does not return until the render observed
//    on entry has completed AND its completion callback has returned, as
//    the spec requires. It deliberately does not wait for renders *started
//    by* that callback (each render is sequence-numbered), so an app thread
//    calling anariFrameReady(ANARI_WAIT) cannot be starved by a callback
//    that keeps chaining new frames. Calls made *from* the callback thread
//    itself skip the callback drain so a callback can re-render/re-query
//    synchronously without deadlocking on its own completion.
//  - plain wait() only waits for the render itself; it is used everywhere a
//    caller may hold per-object or scene locks that a concurrently running
//    callback might need.
//  - at shutdown (device teardown), queued-but-not-yet-invoked callbacks
//    are dropped, never invoked: the ANARIDevice passed to them would be
//    mid-destruction.

struct FrameOutputDriver::Impl
{
  helium::IntrusivePtr<Frame> frame;
  Frame *lastFrame{nullptr};
  std::vector<float4> buffer;
  bool renderFinished{true};
  std::mutex mutex;
  std::condition_variable cv;

  std::chrono::time_point<std::chrono::steady_clock> start;

  // Completion-callback worker state (guarded by 'mutex').
  struct CallbackJob
  {
    ANARIFrameCompletionCallback callback{nullptr};
    const void *userData{nullptr};
    ANARIDevice device{nullptr};
    helium::IntrusivePtr<Frame> frame; // keeps the frame alive until invoked
    uint64_t seq{0}; // renderSeq of the render this job completes
  };
  std::deque<CallbackJob> callbackQueue;
  bool shutdown{false};
  std::condition_variable callbackCv;
  std::thread callbackThread;

  // Render/callback sequence tracking for waitForCallbacks(): renderSeq
  // counts completed renders; callbackSeqQueued/Done are the renderSeq of
  // the most recently queued / finished callback job (jobs are processed in
  // order, so 'done >= s' means every callback for renders <= s returned).
  uint64_t renderSeq{0};
  uint64_t callbackSeqQueued{0};
  uint64_t callbackSeqDone{0};
};

FrameOutputDriver::FrameOutputDriver()
{
  m_impl = std::make_shared<Impl>();
  m_impl->callbackThread = std::thread([this] { callbackThreadLoop(); });
}

FrameOutputDriver::~FrameOutputDriver()
{
  shutdownCallbackThread();
}

// Stops the callback thread; queued-but-not-invoked jobs are dropped (their
// frame retains released). Idempotent -- CyclesDevice::~CyclesDevice() calls
// this while device/session/scene are still fully alive, well before the
// Session destructor destroys this driver.
void FrameOutputDriver::shutdownCallbackThread()
{
  {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->shutdown = true;
  }
  m_impl->callbackCv.notify_all();
  m_impl->cv.notify_all(); // release any waitForCallbacks() waiters
  if (m_impl->callbackThread.joinable())
    m_impl->callbackThread.join();

  // Release dropped jobs' frame retains outside the lock (releasing a frame
  // can cascade into scene-node destruction).
  std::deque<Impl::CallbackJob> dropped;
  {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    dropped.swap(m_impl->callbackQueue);
  }
  dropped.clear();
}

void FrameOutputDriver::callbackThreadLoop()
{
  std::unique_lock<std::mutex> lock(m_impl->mutex);
  for (;;) {
    m_impl->callbackCv.wait(lock,
        [&] { return m_impl->shutdown || !m_impl->callbackQueue.empty(); });
    if (m_impl->shutdown) // never invoke callbacks during teardown
      return;

    auto job = std::move(m_impl->callbackQueue.front());
    m_impl->callbackQueue.pop_front();

    // Invoke without holding the driver mutex -- the callback may call back
    // into this driver (frameReady/renderFrame/map on any frame).
    lock.unlock();
    job.callback(job.userData, job.device, (ANARIFrame)job.frame.ptr);
    job.frame = nullptr; // may destroy the frame; must not hold locks
    lock.lock();

    m_impl->callbackSeqDone = job.seq;
    m_impl->cv.notify_all(); // wake wait()ers draining the callback queue
  }
}

void FrameOutputDriver::write_render_tile(const Tile &tile)
{
  // A previous tile of this render may already have ended the frame (e.g.
  // the partial-tile rejection below) -- nothing to write it to then.
  if (!m_impl->frame)
    return;

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
  extractNormalPass(tile);
  extractAlbedoPass(tile);
  extractObjectIdPass(tile);
  if (frame.m_primitiveIdType == ANARI_UINT32)
    extractAovIdPass(tile, "primitiveId", frame.m_primitiveIdBuffer);
  if (frame.m_instanceIdType == ANARI_UINT32)
    extractAovIdPass(tile, "instanceId", frame.m_instanceIdBuffer);
  renderEnd();
}

bool FrameOutputDriver::renderBegin(Frame *f)
{
  std::lock_guard<std::mutex> lock(m_impl->mutex);
  m_impl->start = std::chrono::steady_clock::now();
  bool frameChanged = m_impl->lastFrame != nullptr && m_impl->lastFrame != f;
  m_impl->frame = f;
  m_impl->lastFrame = f;
  m_impl->renderFinished = false;
  return frameChanged;
}

void FrameOutputDriver::renderEnd()
{
  std::lock_guard<std::mutex> lock(m_impl->mutex);

  auto end = std::chrono::steady_clock::now();
  Frame *f = m_impl->frame.ptr;
  f->m_duration = std::chrono::duration<float>(end - m_impl->start).count();

  const uint64_t seq = ++m_impl->renderSeq;

  // Queue the completion callback (invoked by the callback thread, see the
  // threading notes at the top of this file). The job takes over this
  // driver's frame retention, keeping the frame alive until invoked.
  if (f->m_completionCallback) {
    m_impl->callbackQueue.push_back({f->m_completionCallback,
        f->m_completionCallbackUserData,
        f->deviceState()->anariDevice,
        std::move(m_impl->frame),
        seq});
    m_impl->callbackSeqQueued = seq;
    m_impl->callbackCv.notify_one();
  }

  m_impl->frame = nullptr;
  m_impl->renderFinished = true;

  // Notify the wait thread
  m_impl->cv.notify_all();
}

void FrameOutputDriver::wait()
{
  std::unique_lock<std::mutex> lock(m_impl->mutex);
  m_impl->cv.wait(lock, [&] { return m_impl->renderFinished; });
}

void FrameOutputDriver::waitForCallbacks()
{
  std::unique_lock<std::mutex> lock(m_impl->mutex);
  const bool onCallbackThread =
      std::this_thread::get_id() == m_impl->callbackThread.get_id();

  // The render this caller is waiting on: the one in flight right now, or
  // (when idle) the last completed one. Waiting on a fixed sequence number
  // -- instead of the current renderFinished flag -- keeps this from
  // spinning forever when the completion callback immediately starts the
  // next render (the extension's primary use case).
  const uint64_t targetSeq =
      m_impl->renderFinished ? m_impl->renderSeq : m_impl->renderSeq + 1;

  m_impl->cv.wait(lock, [&] {
    if (m_impl->renderSeq < targetSeq)
      return false; // that render hasn't completed yet
    if (onCallbackThread || m_impl->shutdown)
      return true;
    // ... and its completion callback (if it had one) has returned.
    return m_impl->callbackSeqDone
        >= std::min(m_impl->callbackSeqQueued, targetSeq);
  });
}

bool FrameOutputDriver::ready() const
{
  return m_impl->renderFinished;
}

// Bilinearly sample the baked background image at continuous screen
// coordinates (u, v) in [0, 1], texel-centered and clamped at the edges --
// the KHR_RENDERER_BACKGROUND_IMAGE "image stretched over the frame"
// semantics (mirrors helide's backgroundColorFromImage()).
static math::float4 sampleBackgroundImage(
    const Renderer::BackgroundImage &img, float u, float v)
{
  const auto ix = helium::getInterpolant(u, img.width, true);
  const auto iy = helium::getInterpolant(v, img.height, true);
  auto texel = [&](int32_t x, int32_t y) {
    x = std::clamp(x, 0, int32_t(img.width) - 1);
    y = std::clamp(y, 0, int32_t(img.height) - 1);
    return img.texels[size_t(y) * img.width + size_t(x)];
  };
  const auto v0 = linalg::lerp(
      texel(ix.lower, iy.lower), texel(ix.lower, iy.upper), iy.frac);
  const auto v1 = linalg::lerp(
      texel(ix.upper, iy.lower), texel(ix.upper, iy.upper), iy.frac);
  return linalg::lerp(v0, v1, ix.frac);
}

// Composite the renderer background under the transparent-film combined
// pass (Frame::m_bgComposite documents when this is enabled). The pass is
// premultiplied with alpha = surface coverage, so
//   out.rgb = render.rgb + (1 - coverage) * bg.rgb
//   out.a   = coverage   + (1 - coverage) * bg.a
// which yields exactly (bg.rgb, bg.a) at misses -- the ANARI-specified
// "background alpha is written to channel.color" behavior -- and leaves
// fully covered pixels untouched. Rows run bottom-up in both the tile and
// the (already linear) baked image.
static void compositeBackground(
    const Frame::BackgroundComposite &bg, float *rgba, int width, int height)
{
  const auto *img = bg.image.get();
  parallel_for(0, height, [&](int y) {
    float *px = rgba + size_t(y) * width * 4;
    const float v = (y + 0.5f) / height;
    for (int x = 0; x < width; x++, px += 4) {
      const float t = 1.f - std::clamp(px[3], 0.f, 1.f);
      if (t == 0.f)
        continue; // fully covered -- skip the sample (common interior case)
      const math::float4 b =
          img ? sampleBackgroundImage(*img, (x + 0.5f) / width, v) : bg.color;
      px[0] += t * b.x;
      px[1] += t * b.y;
      px[2] += t * b.z;
      px[3] += t * b.w;
    }
  });
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

  if (m_impl->frame->m_bgComposite.enabled)
    compositeBackground(m_impl->frame->m_bgComposite, dst, width, height);

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

void FrameOutputDriver::extractNormalPass(const Tile &tile)
{
  if (m_impl->frame->m_normalType != ANARI_FLOAT32_VEC3)
    return;

  if (!tile.get_pass_pixels("normal", 3, m_impl->frame->m_normalBuffer.data()))
    m_impl->frame->reportMessage(
        ANARI_SEVERITY_ERROR, "Failed to read 'normal' pass");
}

void FrameOutputDriver::extractAlbedoPass(const Tile &tile)
{
  if (m_impl->frame->m_albedoType != ANARI_FLOAT32_VEC3)
    return;

  if (!tile.get_pass_pixels(
          "diffuse_color", 3, m_impl->frame->m_albedoBuffer.data()))
    m_impl->frame->reportMessage(
        ANARI_SEVERITY_ERROR, "Failed to read 'diffuse_color' pass");
}

void FrameOutputDriver::extractObjectIdPass(const Tile &tile)
{
  if (m_impl->frame->m_objectIdType != ANARI_UINT32)
    return;

  const int numPixels = tile.size.x * tile.size.y;
  std::vector<float> tmp(numPixels);
  if (!tile.get_pass_pixels("object_id", 1, tmp.data())) {
    m_impl->frame->reportMessage(
        ANARI_SEVERITY_ERROR, "Failed to read 'object_id' pass");
    return;
  }

  auto *dst = m_impl->frame->m_objectIdBuffer.data();
  for (int i = 0; i < numPixels; i++) {
    // The pass holds Object::pass_id (an int -- the unset Surface/Volume
    // 'id' ~0u arrives as -1.0f); go through int64 so negative/large values
    // wrap back to the uint32 id instead of being undefined behavior.
    dst[i] = static_cast<uint32_t>(static_cast<int64_t>(tmp[i]));
  }
}

// The 'primitiveId'/'instanceId' channels are value AOVs written by every
// surface material with the id biased by +1 (see Material::makeGraph());
// pixels no surface AOV ever touched (background, volumes) hold 0. AOV
// passes are float-accumulated and averaged over the samples, so ids are
// exact for a 1-sample render; beyond that, interior pixels stay exact as
// long as id * sampleCount stays within float precision (~2^24), while
// pixels whose samples saw different surfaces (or the background) blend
// ids -- best-effort on antialiased edges, like any averaged id pass.
void FrameOutputDriver::extractAovIdPass(
    const Tile &tile, const char *passName, std::vector<uint32_t> &dst)
{
  const int numPixels = tile.size.x * tile.size.y;
  std::vector<float> tmp(numPixels);
  if (!tile.get_pass_pixels(passName, 1, tmp.data())) {
    m_impl->frame->reportMessage(
        ANARI_SEVERITY_ERROR, "Failed to read '%s' pass", passName);
    return;
  }

  for (int i = 0; i < numPixels; i++) {
    // llround: plain lround is 32-bit on some platforms and biased ids can
    // legitimately reach 2^32.
    const long long biased = std::llround(static_cast<double>(tmp[i]));
    dst[i] = (biased <= 0 || biased > 0x100000000ll)
        ? ~0u
        : static_cast<uint32_t>(biased - 1);
  }
}

} // namespace anari_cycles
