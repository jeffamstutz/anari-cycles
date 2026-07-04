// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Frame.h"

namespace anari_cycles {

Frame::Frame(CyclesGlobalState *s) : helium::BaseFrame(s) {}

Frame::~Frame()
{
  // No wait() here: the output driver retains the frame while it is in
  // flight, so reaching this destructor means the frame cannot be rendering.
  // Waiting on the (shared) output driver could deadlock -- e.g. when this
  // frame is destroyed by the commit-buffer flush inside another frame's
  // renderFrame(), after renderBegin() already marked that frame in-flight
  // (and while the scene lock is held).
}

bool Frame::isValid() const
{
  return m_renderer && m_renderer->isValid() && m_camera && m_camera->isValid()
      && m_world && m_world->isValid();
}

CyclesGlobalState *Frame::deviceState() const
{
  return (CyclesGlobalState *)helium::BaseObject::m_state;
}

void Frame::commitParameters()
{
  auto *world = getParamObject<World>("world");
  if (m_world.ptr != world)
    m_worldLastChanged = 0;
  m_world = world;

  m_renderer = getParamObject<Renderer>("renderer");
  m_camera = getParamObject<Camera>("camera");
  m_colorType = getParam<anari::DataType>("channel.color", ANARI_UNKNOWN);
  m_depthType = getParam<anari::DataType>("channel.depth", ANARI_UNKNOWN);
  m_normalType = getParam<anari::DataType>("channel.normal", ANARI_UNKNOWN);
  m_albedoType = getParam<anari::DataType>("channel.albedo", ANARI_UNKNOWN);
  m_objectIdType = getParam<anari::DataType>("channel.objectId", ANARI_UNKNOWN);
  m_accumulation = getParam<bool>("accumulation", false);
  m_frameData.size = getParam<uint2>("size", make_uint2(10, 10));
}

void Frame::finalize()
{
  if (!m_renderer) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'renderer' on frame");
  }
  if (!m_camera) {
    reportMessage(
        ANARI_SEVERITY_WARNING, "missing required parameter 'camera' on frame");
  }
  if (!m_world) {
    reportMessage(
        ANARI_SEVERITY_WARNING, "missing required parameter 'world' on frame");
  }

  const auto numPixels = m_frameData.size.x * m_frameData.size.y;
  m_perPixelBytes = 4 * (m_colorType == ANARI_FLOAT32_VEC4 ? 4 : 1);
  m_pixelBuffer.resize(numPixels * m_perPixelBytes);
  std::fill(m_pixelBuffer.begin(), m_pixelBuffer.end(), ~0);
  m_depthBuffer.resize(m_depthType == ANARI_FLOAT32 ? numPixels : 0);
  m_normalBuffer.resize(m_normalType == ANARI_FLOAT32_VEC3 ? numPixels * 3 : 0);
  m_albedoBuffer.resize(m_albedoType == ANARI_FLOAT32_VEC3 ? numPixels * 3 : 0);
  m_objectIdBuffer.resize(m_objectIdType == ANARI_UINT32 ? numPixels : 0);
}

bool Frame::getProperty(const std::string_view &name,
    ANARIDataType type,
    void *ptr,
    uint64_t size,
    uint32_t flags)
{
  if (type == ANARI_FLOAT32 && name == "duration") {
    helium::writeToVoidP(ptr, m_duration);
    return true;
  } else if (type == ANARI_INT32 && name == "numSamples") {
    helium::writeToVoidP(ptr, int(deviceState()->sessionSamples));
    return true;
  } else if (type == ANARI_BOOL && name == "nextFrameReset") {
    if (ready()) {
      CyclesGlobalState::SceneLock lock(*deviceState());
      deviceState()->commitBuffer.flush();
    }
    bool doReset = resetAccumulationNextFrame();
    helium::writeToVoidP(ptr, doReset);
    return true;
  }

  return false;
}

void Frame::renderFrame()
{
  auto &state = *deviceState();
  state.waitOnCurrentFrame();

  bool currentFrameChanged = state.output_driver->renderBegin(this);

  // Everything from the commit-buffer flush through session reset/sample
  // setup mutates the Cycles scene, which the render session thread reads
  // under scene->mutex -- hold it for the whole section (see SceneLock docs).
  // The lock must be released before any wait on the render thread below.
  {
    CyclesGlobalState::SceneLock sceneLock(state);

    state.commitBuffer.flush();

    if (!isValid()) {
      reportMessage(
          ANARI_SEVERITY_ERROR, "skipping render of incomplete frame object");
      std::fill(m_pixelBuffer.begin(), m_pixelBuffer.end(), 0);
      state.output_driver->renderEnd(); // cycles render thread not going to run
      return;
    }

    if (m_worldLastChanged < state.objectUpdates.lastSceneChange) {
      reportMessage(ANARI_SEVERITY_DEBUG, "frame -- updating world");
      m_world->setCyclesWorldObjects();
      // scene->objects no longer references retired nodes -- safe to delete
      state.purgeRetiredGeometry();
      m_worldLastChanged = helium::newTimeStamp();
    }

    if (currentFrameChanged || resetAccumulationNextFrame()) {
      reportMessage(ANARI_SEVERITY_DEBUG, "frame -- resetting accumulation");

      state.objectUpdates.lastAccumulationReset = helium::newTimeStamp();

      m_camera->setCameraCurrent(m_frameData.size.x, m_frameData.size.y);
      m_renderer->makeRendererCurrent();

      state.buffer_params.width = m_frameData.size.x;
      state.buffer_params.height = m_frameData.size.y;
      state.buffer_params.full_width = m_frameData.size.x;
      state.buffer_params.full_height = m_frameData.size.y;

      // The sample target must be in the (delayed) reset params -- a later
      // set_samples() would be clobbered when the reset is applied on the
      // render thread (Session::delayed_reset_buffer_params()).
      state.session_params.samples = m_renderer->pixelSamples();
      state.session->reset(state.session_params, state.buffer_params);
      state.sessionSamples = 0;
    }

    state.sessionSamples += m_renderer->pixelSamples();
    state.session->set_samples(state.sessionSamples);
  }

  state.session->start();

  // NOTE(jda): Everything is still implemented as asynchronous, but on some
  //            machines performance plummets (render thread de-prioritized?),
  //            which doesn't happen if we immediately synchronize.
  //
  // TODO: Investigate how to keep performance and maintain asynchronicity...
  if (!m_renderer->runAsync())
    wait();
}

void *Frame::map(std::string_view channel,
    uint32_t *width,
    uint32_t *height,
    ANARIDataType *pixelType)
{
  wait();

  *width = m_frameData.size.x;
  *height = m_frameData.size.y;

  if (channel == "channel.color") {
    *pixelType = m_colorType;
    return m_pixelBuffer.data();
  } else if (channel == "channel.depth") {
    *pixelType = ANARI_FLOAT32;
    return m_depthBuffer.data();
  } else if (channel == "channel.normal") {
    *pixelType = ANARI_FLOAT32_VEC3;
    return m_normalBuffer.data();
  } else if (channel == "channel.albedo") {
    *pixelType = ANARI_FLOAT32_VEC3;
    return m_albedoBuffer.data();
  } else if (channel == "channel.objectId") {
    *pixelType = ANARI_UINT32;
    return m_objectIdBuffer.data();
  } else {
    *width = 0;
    *height = 0;
    *pixelType = ANARI_UNKNOWN;
    return nullptr;
  }
}

void Frame::unmap(std::string_view channel)
{
  // no-op
}

int Frame::frameReady(ANARIWaitMask m)
{
  if (m == ANARI_NO_WAIT)
    return ready();
  else {
    wait();
    return 1;
  }
}

void Frame::discard()
{
  // no-op
}

bool Frame::ready() const
{
  return deviceState()->output_driver->ready();
}

void Frame::wait() const
{
  deviceState()->output_driver->wait();
}

bool Frame::resetAccumulationNextFrame() const
{
  // Without KHR_FRAME_ACCUMULATION enabled ('accumulation' = false), every
  // anariRenderFrame() renders from scratch instead of refining the previous
  // result.
  if (!m_accumulation)
    return true;
  auto *state = deviceState();
  return state->objectUpdates.lastAccumulationReset
      < state->commitBuffer.lastObjectFinalization();
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Frame *);
