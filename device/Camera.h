// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

namespace anari_cycles {

struct Camera : public Object {

  Camera(CyclesGlobalState *s);
  ~Camera() override;

  static Camera *createInstance(std::string_view type, CyclesGlobalState *state);

  virtual void commitParameters() override;
  virtual void finalize() override;

  virtual void setCameraCurrent(int width, int height);

 protected:
  enum class StereoMode
  {
    NONE,
    LEFT,
    RIGHT
  };

  virtual ccl::Transform getMatrix() const;

  // Subtypes that map stereo onto Cycles' own stereo support (panorama
  // spherical stereo) return true so the base class skips the manual
  // eye-position offset in getMatrix().
  virtual bool usesNativeStereo() const;
  // Signed world-space offset of the active eye along the camera right
  // vector (0 for mono or native-stereo subtypes).
  float stereoEyeOffset() const;

  anari_vec::float3 m_pos;
  anari_vec::float3 m_dir;
  anari_vec::float3 m_up;

  // KHR_CAMERA_DEPTH_OF_FIELD
  float m_apertureRadius{0.f};
  float m_focusDistance{1.f};
  // Cycles vendor extensions: polygonal bokeh
  int m_apertureBlades{0};
  float m_apertureRotation{0.f};
  // KHR_CAMERA_SHUTTER -- interval within the frame time domain [0,1]
  helium::box1 m_shutter{0.5f, 0.5f};
  // KHR_CAMERA_ROLLING_SHUTTER
  bool m_rollingShutterDown{false};
  float m_rollingShutterDuration{0.f};
  // KHR_CAMERA_STEREO
  StereoMode m_stereoMode{StereoMode::NONE};
  float m_interpupillaryDistance{0.0635f};
};

}  // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Camera *, ANARI_CAMERA);
