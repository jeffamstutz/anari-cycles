// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Frame.h"

namespace anari_cycles {

Frame::Frame(CyclesGlobalState *s) : helium::BaseFrame(s) {}

Frame::~Frame()
{
  wait();
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
}

bool Frame::getProperty(
    const std::string_view &name, ANARIDataType type, void *ptr, uint32_t flags)
{
  if (type == ANARI_FLOAT32 && name == "duration") {
    helium::writeToVoidP(ptr, m_duration);
    return true;
  } else if (type == ANARI_INT32 && name == "numSamples") {
    helium::writeToVoidP(ptr, int(deviceState()->sessionSamples));
    return true;
  } else if (type == ANARI_BOOL && name == "nextFrameReset") {
    if (ready())
      deviceState()->commitBuffer.flush();
    bool doReset = resetAccumulationNextFrame();
    helium::writeToVoidP(ptr, doReset);
    return true;
  }

  return 0;
}

void Frame::renderFrame()
{
  auto &state = *deviceState();
  state.waitOnCurrentFrame();

  bool currentFrameChanged = state.output_driver->renderBegin(this);

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

    state.session->reset(state.session_params, state.buffer_params);
    state.sessionSamples = 0;
  }

  state.session->set_samples(++state.sessionSamples);
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
  auto *state = deviceState();
  return state->objectUpdates.lastAccumulationReset
      < state->commitBuffer.lastObjectFinalization();
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Frame *);
