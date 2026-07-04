// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Array.h"
#include "MotionTrack.h"
#include "Object.h"

namespace anari_cycles {

struct Camera : public Object {

  Camera(CyclesGlobalState *s);
  ~Camera() override;

  static Camera *createInstance(std::string_view type, CyclesGlobalState *state);

  virtual void commitParameters() override;
  virtual void finalize() override;

  virtual void setCameraCurrent(int width, int height);

  // The committed KHR_CAMERA_SHUTTER interval, normalized: an invalid
  // interval (upper < lower) collapses to the degenerate [lower, lower].
  helium::box1 shutter() const;

 protected:
  enum class StereoMode
  {
    NONE,
    LEFT,
    RIGHT
  };

  // Camera-to-world matrix for an arbitrary pose expressed as ANARI
  // position/direction/up (applies the stereo eye offset along the pose's
  // right vector and the subtype's matrixCorrection()).
  ccl::Transform poseMatrix(const anari_vec::float3 &pos,
      const anari_vec::float3 &dir,
      const anari_vec::float3 &up) const;
  // poseMatrix() of the committed position/direction/up parameters.
  ccl::Transform getMatrix() const;
  // Constant right-multiplied correction mapping the subtype's native Cycles
  // camera space onto the ANARI pos/dir/up frame (identity except for
  // panorama subtypes).
  virtual ccl::Transform matrixCorrection() const;

  // Subtypes that map stereo onto Cycles' own stereo support (panorama
  // spherical stereo) return true so the base class skips the manual
  // eye-position offset in poseMatrix().
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
  // KHR_CAMERA_MOTION_TRANSFORMATION -- when non-empty, the track overrides
  // position/direction/up: each key transforms the canonical camera frame
  // (origin, direction (0,0,-1), up (0,1,0)) into world space.
  helium::ChangeObserverPtr<Array1D> m_motionTransform;
  helium::ChangeObserverPtr<Array1D> m_motionScale;
  helium::ChangeObserverPtr<Array1D> m_motionRotation;
  helium::ChangeObserverPtr<Array1D> m_motionTranslation;
  MotionTrack m_motion;
};

}  // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Camera *, ANARI_CAMERA);
