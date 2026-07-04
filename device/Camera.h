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
  ccl::Transform getMatrix() const;

  anari_vec::float3 m_pos;
  anari_vec::float3 m_dir;
  anari_vec::float3 m_up;

  // KHR_CAMERA_DEPTH_OF_FIELD
  float m_apertureRadius{0.f};
  float m_focusDistance{1.f};
  // Cycles vendor extensions: polygonal bokeh
  int m_apertureBlades{0};
  float m_apertureRotation{0.f};
};

}  // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Camera *, ANARI_CAMERA);
