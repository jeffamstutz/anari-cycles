// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Camera.h"
// cycles
#include "scene/camera.h"
// std
#include <algorithm>

namespace anari_cycles {

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

struct Omnidirectional : public Camera
{
  Omnidirectional(CyclesGlobalState *s);

  void commitParameters() override;
  void setCameraCurrent(int width, int height) override;

 protected:
  ccl::Transform getMatrix() const override;
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
  else if (type == "omnidirectional")
    return new Omnidirectional(s);
  else
    return (Camera *)new UnknownObject(ANARI_CAMERA, type, s);
}

void Camera::commitParameters()
{
  m_pos = getParam<anari_vec::float3>("position", {0.f, 0.f, 0.f});
  m_dir = getParam<anari_vec::float3>("direction", {0.f, 0.f, -1.f});
  m_up = getParam<anari_vec::float3>("up", {0.f, 1.f, 0.f});
  // KHR_CAMERA_DEPTH_OF_FIELD -- ANARI 'apertureRadius' is the lens radius in
  // world units, which is exactly what Cycles 'aperturesize' scales its
  // unit-disk lens samples by (kernel/camera/camera.h).
  m_apertureRadius = getParam<float>("apertureRadius", 0.f);
  m_focusDistance = getParam<float>("focusDistance", 1.f);
  // Vendor params: polygonal bokeh (Cycles clamps blades < 3 to a disk)
  m_apertureBlades = getParam<int>("apertureBlades", 0);
  m_apertureRotation = getParam<float>("apertureRotation", 0.f);
}

void Camera::finalize()
{
  Object::finalize();
}

void Camera::setCameraCurrent(int width, int height)
{
  auto &state = *deviceState();
  state.scene->camera->set_matrix(getMatrix());
  state.scene->camera->set_full_width(width);
  state.scene->camera->set_full_height(height);
  state.scene->camera->set_aperturesize(std::max(m_apertureRadius, 0.f));
  state.scene->camera->set_focaldistance(m_focusDistance);
  state.scene->camera->set_blades(
      static_cast<unsigned int>(std::max(m_apertureBlades, 0)));
  state.scene->camera->set_bladesrotation(m_apertureRotation);
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

// Omnidirectional definitions ////////////////////////////////////////////////

Omnidirectional::Omnidirectional(CyclesGlobalState *s) : Camera(s) {}

void Omnidirectional::commitParameters()
{
  Camera::commitParameters();
  // KHR_CAMERA_OMNIDIRECTIONAL only defines "equirectangular"; anything else
  // is unsupported, so warn and fall back to it. 'imageRegion' is not
  // implemented by any camera subtype in this device yet, so it is
  // (deliberately) not read here either.
  auto layout = getParamString("layout", "equirectangular");
  if (layout != "equirectangular") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "unsupported omnidirectional camera 'layout' value '%s' -- "
        "falling back to 'equirectangular'",
        layout.c_str());
  }
}

void Omnidirectional::setCameraCurrent(int width, int height)
{
  Camera::setCameraCurrent(width, height);
  auto &state = *deviceState();
  state.scene->camera->set_camera_type(ccl::CameraType::CAMERA_PANORAMA);
  state.scene->camera->set_panorama_type(ccl::PANORAMA_EQUIRECTANGULAR);
  // Panorama rays are generated from screen coordinates in [0,1]^2 (see
  // kernel/camera/camera.h camera_panorama_direction); reset the viewplane in
  // case another camera subtype left a perspective/ortho viewplane behind.
  state.scene->camera->viewplane.left = 0.f;
  state.scene->camera->viewplane.right = 1.f;
  state.scene->camera->viewplane.bottom = 0.f;
  state.scene->camera->viewplane.top = 1.f;
}

ccl::Transform Omnidirectional::getMatrix() const
{
  // Cycles' equirectangular kernel mapping (kernel/camera/projection.h,
  // equirectangular_range_to_direction with the default longitude/latitude
  // range) places the image center along camera-space +X, the image top pole
  // along +Z, and the right half of the image toward -Y. The base look-at
  // matrix maps (right, up, dir) onto camera-space (+X, +Y, +Z), so append a
  // constant rotation taking panorama camera space into that frame
  // (+X -> +Z, +Y -> -X, +Z -> +Y). The result honors the ANARI convention:
  // image center looks along 'direction', the top pole is '+up' (and
  // left/right of the image are the viewer's left/right).
  // clang-format off
  return Camera::getMatrix()
      * ccl::make_transform(
          0.f, -1.f, 0.f, 0.f,
          0.f,  0.f, 1.f, 0.f,
          1.f,  0.f, 0.f, 0.f);
  // clang-format on
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Camera *);
