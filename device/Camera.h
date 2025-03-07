// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

namespace cycles {

struct Camera : public Object {

  Camera(CyclesGlobalState *s);
  ~Camera() override;

  static Camera *createInstance(std::string_view type, CyclesGlobalState *state);

  void commitParameters() override;
  virtual void setCameraCurrent(int width, int height);

 protected:
  ccl::Transform getMatrix() const;

  anari_vec::float3 m_pos;
  anari_vec::float3 m_dir;
  anari_vec::float3 m_up;
};

}  // namespace cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(cycles::Camera *, ANARI_CAMERA);
