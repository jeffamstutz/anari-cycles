// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Array.h"
#include "Object.h"
// std
#include <memory>
#include <vector>

namespace anari_cycles {

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
  int m_pixelSamples{1};

  void rebuildDefaultBackgroundShader();
  void rebakeBackgroundImage();
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Renderer *, ANARI_RENDERER);
