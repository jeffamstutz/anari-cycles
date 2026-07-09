// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "camera/Camera.h"
#include "renderer/Renderer.h"
#include "world/World.h"
// cycles
#include "scene/pass.h"
// helium
#include "helium/BaseFrame.h"
// std
#include <memory>
#include <string>
#include <vector>

namespace anari_cycles {

struct Frame : public helium::BaseFrame
{
  // Renderer-background compositing snapshot for the most recent render of
  // this frame (taken in renderFrame() before the session starts; read by
  // the output driver when the tile arrives). 'enabled' mirrors the Cycles
  // film's transparent flag: the combined pass then holds premultiplied
  // color with coverage alpha, and the output driver composites the
  // background color/image (with its alpha) underneath -- see
  // FrameOutputDriver::extractColorPass().
  struct BackgroundComposite
  {
    bool enabled{false};
    math::float4 color{0.f, 0.f, 0.f, 1.f};
    std::shared_ptr<const Renderer::BackgroundImage> image; // null -> color
  };

  // CYCLES_LIGHTGROUPS: one entry per 'channel.lightgroup.<name>' frame
  // channel (ANARI_FLOAT32_VEC3), backed by a Cycles per-lightgroup combined
  // pass created on demand (see syncLightgroupPasses()).
  struct LightgroupChannel
  {
    std::string name; // the <name> suffix == Cycles lightgroup name
    std::string passName; // "lightgroup_<name>" scene pass backing it
    std::vector<float> buffer; // RGB
  };

  // CYCLES_FRAME_CHANNELS: static description of one vendor frame channel
  // backed by a Cycles pass created on demand (see syncAuxPasses()). The
  // table of supported channels lives in Frame.cpp.
  struct AuxChannelDesc
  {
    const char *channel; // ANARI frame parameter, e.g. "channel.position"
    const char *passName; // Cycles scene pass name backing it
    ccl::PassType passType;
    anari::DataType type; // the one data type the channel supports
    int components; // floats per pixel in the Cycles pass
    bool scaleBySamples; // sampleCount: rescale normalized value to counts
  };

  // One entry per requested vendor frame channel this commit.
  struct AuxChannel
  {
    const AuxChannelDesc *desc{nullptr};
    std::vector<float> buffer; // desc->components floats per pixel
    // Whether the backing pass exists for the current render; syncAuxPasses()
    // clears this for 'channel.shadowCatcherMatte' when the scene has no
    // shadow-catcher objects (the channel then reads zero).
    bool active{true};
  };

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
  // Make the scene's set of per-lightgroup combined passes match this
  // frame's 'channel.lightgroup.*' channels (runs under the SceneLock on
  // every accumulation reset).
  void syncLightgroupPasses();
  // Same reconciliation for the CYCLES_FRAME_CHANNELS aux passes (position,
  // mist, motion, ...): passes for dropped channels are deleted, missing
  // ones created -- unused channels cost nothing.
  void syncAuxPasses();

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
  std::vector<LightgroupChannel> m_lightgroupChannels;
  std::vector<AuxChannel> m_auxChannels;

  BackgroundComposite m_bgComposite;

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
