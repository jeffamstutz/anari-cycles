// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Perspective.h"
// cycles
#include "scene/camera.h"

namespace anari_cycles {

Perspective::Perspective(CyclesGlobalState *s) : Camera(s) {}

void Perspective::commitParameters()
{
  Camera::commitParameters();
  m_fovy = getParam<float>("fovy", radians(60.f));
  m_aspect = getParam<float>("aspect", 1.f);
}

void Perspective::setCameraCurrent(int width, int height)
{
  Camera::setCameraCurrent(width, height);
  auto &state = *deviceState();
  state.scene->camera->viewplane.left = -m_aspect;
  state.scene->camera->viewplane.right = m_aspect;
  state.scene->camera->viewplane.bottom = -1.0f;
  state.scene->camera->viewplane.top = 1.0f;
  state.scene->camera->set_fov(m_fovy);
  // Keep the motion-pass FOV endpoints in sync (their Cycles defaults are
  // pi/4): fov != fov_pre would make the 'channel.motion' vectors project
  // shutter-open/close positions through a different FOV, yielding bogus
  // constant motion on entirely static scenes.
  state.scene->camera->set_fov_pre(m_fovy);
  state.scene->camera->set_fov_post(m_fovy);
  state.scene->camera->set_camera_type(ccl::CameraType::CAMERA_PERSPECTIVE);
}

} // namespace anari_cycles
