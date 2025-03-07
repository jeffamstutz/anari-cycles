// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Camera.h"
// cycles
#include "scene/camera.h"

namespace cycles {

// Subtype declarations ///////////////////////////////////////////////////////

struct Perspective : public Camera
{
  Perspective(CyclesGlobalState *s);

  void commitParameters() override;
  void setCameraCurrent(int width, int height) override;

 private:
  float m_fovy{radians(60.f)};
  float m_aspect{1.f};
};

struct Orthographic : public Camera
{
  Orthographic(CyclesGlobalState *s);

  void commitParameters() override;
  void setCameraCurrent(int width, int height) override;

 private:
  float m_height{1.f};
  float m_aspect{1.f};
};

// Camera definitions /////////////////////////////////////////////////////////

Camera::Camera(CyclesGlobalState *s) : Object(ANARI_CAMERA, s) {}

Camera::~Camera() = default;

Camera *Camera::createInstance(std::string_view type, CyclesGlobalState *s)
{
  if (type == "perspective")
    return new Perspective(s);
  else if (type == "orthographic")
    return new Orthographic(s);
  else
    return (Camera *)new UnknownObject(ANARI_CAMERA, type, s);
}

void Camera::commitParameters()
{
  m_pos = getParam<anari_vec::float3>("position", {0.f, 0.f, 0.f});
  m_dir = getParam<anari_vec::float3>("direction", {0.f, 0.f, 1.f});
  m_up = getParam<anari_vec::float3>("up", {0.f, 1.f, 0.f});
}

void Camera::setCameraCurrent(int width, int height)
{
  auto &state = *deviceState();
  state.scene->camera->set_matrix(getMatrix());
  state.scene->camera->set_full_width(width);
  state.scene->camera->set_full_height(height);
  state.scene->camera->need_flags_update = true;
  state.scene->camera->need_device_update = true;
}

ccl::Transform Camera::getMatrix() const
{
  ccl::Transform retval;

  auto dir = normalize(ccl::make_float3(m_dir[0], m_dir[1], m_dir[2]));
  auto pos = ccl::make_float3(m_pos[0], m_pos[1], m_pos[2]);
  auto up = normalize(ccl::make_float3(m_up[0], m_up[1], m_up[2]));

  const auto s = ccl::normalize(ccl::cross(dir, up));
  const auto u = ccl::normalize(ccl::cross(s, dir));
  retval.x[0] = s.x;
  retval.x[1] = u.x;
  retval.x[2] = dir.x;
  retval.y[0] = s.y;
  retval.y[1] = u.y;
  retval.y[2] = dir.y;
  retval.z[0] = s.z;
  retval.z[1] = u.z;
  retval.z[2] = dir.z;
  retval.x[3] = pos.x;
  retval.y[3] = pos.y;
  retval.z[3] = pos.z;
  return retval;
}

// Perspective definitions ////////////////////////////////////////////////////

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
  state.scene->camera->set_camera_type(ccl::CameraType::CAMERA_PERSPECTIVE);
}

// Orthographic definitions ///////////////////////////////////////////////////

Orthographic::Orthographic(CyclesGlobalState *s) : Camera(s) {}

void Orthographic::commitParameters()
{
  Camera::commitParameters();
  m_height = getParam<float>("height", 1.f);
  m_aspect = getParam<float>("aspect", 1.f);
}

void Orthographic::setCameraCurrent(int width, int height)
{
  Camera::setCameraCurrent(width, height);
  auto &state = *deviceState();
  state.scene->camera->set_camera_type(ccl::CameraType::CAMERA_ORTHOGRAPHIC);
  auto scale = m_height / 2.f;
  state.scene->camera->viewplane.left = -m_aspect * scale;
  state.scene->camera->viewplane.right = m_aspect * scale;
  state.scene->camera->viewplane.bottom = -scale;
  state.scene->camera->viewplane.top = scale;
}

} // namespace cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(cycles::Camera *);
