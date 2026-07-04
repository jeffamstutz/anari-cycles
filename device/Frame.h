// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Camera.h"
#include "Renderer.h"
#include "World.h"
// helium
#include "helium/BaseFrame.h"
// std
#include <vector>

namespace anari_cycles {

struct Frame : public helium::BaseFrame
{
  Frame(CyclesGlobalState *s);
  ~Frame() override;

  bool isValid() const override;

  CyclesGlobalState *deviceState() const;

  bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint64_t size,
      uint32_t flags) override;

  void commitParameters() override;
  void finalize() override;

  void renderFrame() override;

  void *map(std::string_view channel,
      uint32_t *width,
      uint32_t *height,
      ANARIDataType *pixelType) override;
  void unmap(std::string_view channel) override;
  int frameReady(ANARIWaitMask m) override;
  void discard() override;

  bool ready() const;
  void wait() const;

 private:
  bool resetAccumulationNextFrame() const;

  friend struct FrameOutputDriver;

  //// Data ////

  bool m_valid{false};
  bool m_ready{true};
  int m_perPixelBytes{1};

  struct FrameData
  {
    uint2 size;
  } m_frameData;

  bool m_accumulation{false};

  anari::DataType m_colorType{ANARI_UNKNOWN};
  anari::DataType m_depthType{ANARI_UNKNOWN};
  anari::DataType m_normalType{ANARI_UNKNOWN};
  anari::DataType m_albedoType{ANARI_UNKNOWN};
  anari::DataType m_objectIdType{ANARI_UNKNOWN};
  anari::DataType m_primitiveIdType{ANARI_UNKNOWN};
  anari::DataType m_instanceIdType{ANARI_UNKNOWN};

  std::vector<uint8_t> m_pixelBuffer;
  std::vector<float> m_depthBuffer;
  std::vector<float> m_normalBuffer;
  std::vector<float> m_albedoBuffer;
  std::vector<uint32_t> m_objectIdBuffer;
  std::vector<uint32_t> m_primitiveIdBuffer;
  std::vector<uint32_t> m_instanceIdBuffer;

  // KHR_FRAME_COMPLETION_CALLBACK: invoked by the FrameOutputDriver's
  // callback thread after each render of this frame finishes.
  ANARIFrameCompletionCallback m_completionCallback{nullptr};
  const void *m_completionCallbackUserData{nullptr};

  // Sample interval covered by the most recent renderFrame() ('base' ->
  // 'target' cumulative session samples), used to derive the frame-local
  // 'renderProgress' property from the session's Progress.
  size_t m_progressSampleBase{0};
  size_t m_progressSampleTarget{0};

  helium::IntrusivePtr<Renderer> m_renderer;
  helium::IntrusivePtr<Camera> m_camera;
  helium::IntrusivePtr<World> m_world;

  float m_duration{0.f};

  bool m_frameChanged{false};
  helium::TimeStamp m_cameraLastChanged{0};
  helium::TimeStamp m_rendererLastChanged{0};
  helium::TimeStamp m_worldLastChanged{0};
  helium::TimeStamp m_lastCommitOccured{0};
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Frame *, ANARI_FRAME);
