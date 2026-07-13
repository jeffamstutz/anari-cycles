// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "array/Array2D.h"
#include "Object.h"
// std
#include <memory>
#include <string>
#include <vector>

namespace anari_cycles {

// CYCLES_RENDERER_DENOISE_START: name of the noisy combined pass kept in
// the scene while denoising is on (see Renderer::syncNoisyColorPass()); the
// output driver reads it for 'channel.color' below the 'denoiseStart'
// threshold.
constexpr const char *g_noisyCombinedPassName = "combined_noisy";

struct Renderer : public Object
{
  // CPU copy of the 'background' image (KHR_RENDERER_BACKGROUND_IMAGE),
  // converted to linear float RGBA (straight alpha) with the ANARI attribute
  // conversion rules; row 0 is the bottom row, like the frame buffer. The
  // output driver bilinearly samples it in screen space to composite it
  // under the transparent-film render -- see
  // FrameOutputDriver::extractColorPass().
  struct BackgroundImage
  {
    uint32_t width{0};
    uint32_t height{0};
    std::vector<math::float4> texels;
  };

  Renderer(CyclesGlobalState *s);
  ~Renderer() override;

  void commitParameters() override;

  void makeRendererCurrent();

  bool runAsync() const;
  int pixelSamples() const;

  // Whether 'denoise' is on and a denoiser is compiled in. Only current
  // after makeRendererCurrent().
  bool denoiseEnabled() const;
  // CYCLES_RENDERER_DENOISE_START: accumulated sample count at which
  // 'channel.color' switches from the raw accumulation to the denoised
  // result (never negative; 0 -- the default -- denoises from the first
  // sample). Consumed by Frame::renderFrame().
  int denoiseStart() const;

  // CYCLES_RENDERER_INTERACTIVE_SCALING: opt-in low-res preview frames on
  // accumulation resets (camera/scene changes); consumed by
  // Frame::renderFrame(), which only engages it while the frame uses
  // 'accumulation'.
  bool interactiveScaling() const;
  int interactiveScalingDivider() const; // fixed divider; 0 -> automatic
  float interactiveScalingTargetFrameTime() const; // seconds

  math::float3 backgroundColor() const;
  math::float4 backgroundColorAndAlpha() const;

  // Whether honoring the 'background' parameter requires rendering on a
  // transparent film and compositing in the output driver: true for any
  // image background, or a background color with alpha < 1. An opaque
  // background color is rendered by the background shader directly (the
  // combined pass alpha is 1 everywhere, which already matches the spec).
  bool backgroundNeedsCompositing() const;

  // The baked background image; nullptr when 'background' is a plain color
  // (or the image's element type is unsupported). Only current after
  // makeRendererCurrent().
  std::shared_ptr<const BackgroundImage> backgroundImage() const;

 private:
  struct {
    int background : 1;
    int backgroundImage : 1; // ARRAY2D form changed -> rebake CPU copy
    int ambientLight : 1;
    int denoise : 1;
  } m_needsUpdateStatus = {true, true, true, true};

  math::float4 m_backgroundColor;
  helium::IntrusivePtr<Array2D> m_backgroundImageArray;
  std::shared_ptr<const BackgroundImage> m_backgroundImage;
  math::float3 m_ambientColor;
  float m_ambientRadiance;
  bool m_runAsync{false};
  bool m_denoise{false};
  int m_denoiseStart{0};
  int m_pixelSamples{1};

  // CYCLES_RENDERER_INTERACTIVE_SCALING parameters (defaults keep the
  // feature off -- behavior is then identical to not having it at all).
  struct {
    bool enabled{true};
    int divider{0}; // fixed divider override; 0 -> automatic
    float targetFrameTime{1.f / 30.f}; // automatic-divider target, seconds
  } m_interactive;

  // Vendor sampling/integrator controls (CYCLES_RENDERER_SAMPLING_CONTROLS).
  // Defaults mirror the Cycles Integrator/Film socket defaults so leaving
  // the parameters unset changes nothing. Pushed to the shared scene by
  // pushSamplingState() -- the Cycles setters no-op (no tag_modified) when
  // the value is unchanged, so pushing every accumulation reset is free.
  struct {
    int maxBounce{7};
    int maxDiffuseBounce{7};
    int maxGlossyBounce{7};
    int maxTransmissionBounce{7};
    int maxVolumeBounce{7};
    int maxTransparencyBounce{7}; // Cycles 'transparent_max_bounce'
    float clampDirect{0.f}; // 0 disables
    float clampIndirect{10.f}; // 0 disables
    bool lightTree{true};
    float lightSamplingThreshold{0.f};
    bool causticsReflective{true};
    bool causticsRefractive{true};
    float filterGlossy{0.f};
    int aoBounces{0}; // 0 disables fast-GI approximation
    float aoFactor{0.f};
    float aoDistance{3.402823466e38f}; // FLT_MAX
    bool adaptiveSampling{true};
    float adaptiveThreshold{0.01f};
    int adaptiveMinSamples{0}; // 0 -> automatic
    float exposure{1.f};
    std::string pixelFilter{"box"}; // box | gaussian | blackmanHarris
    float pixelFilterWidth{1.f};
    // CYCLES_FRAME_CHANNELS 'channel.mist' distance mapping (Film sockets).
    float mistStart{0.f};
    float mistDepth{100.f};
    float mistFalloff{1.f};
  } m_sampling;

  void rebuildDefaultBackgroundShader();
  void rebakeBackgroundImage();
  void syncNoisyColorPass();
  void pushSamplingState();
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Renderer *, ANARI_RENDERER);
