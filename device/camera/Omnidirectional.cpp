// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Omnidirectional.h"
// cycles
#include "scene/camera.h"

namespace anari_cycles {

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

  // KHR_CAMERA_STEREO: use Cycles' native spherical stereo for the panorama
  // camera -- it rotates the per-eye offset with the view direction (correct
  // 360 stereo) and works in a single render pass (the kernel only checks
  // interocular_offset, no multi-view machinery involved). In the panorama
  // path spherical_stereo_transform runs in *camera* space where its
  // hardcoded up axis (0,0,1) is exactly the axis this camera's getMatrix()
  // maps to the ANARI 'up' direction, so arbitrary orientations are fine
  // (unlike the perspective path). interocular_distance and parallel
  // convergence were already set by the base class.
  if (m_stereoMode != StereoMode::NONE) {
    state.scene->camera->set_use_spherical_stereo(true);
    state.scene->camera->set_stereo_eye(m_stereoMode == StereoMode::LEFT
            ? ccl::Camera::STEREO_LEFT
            : ccl::Camera::STEREO_RIGHT);
  }
}

bool Omnidirectional::usesNativeStereo() const
{
  return true;
}

ccl::Transform Omnidirectional::matrixCorrection() const
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
  return ccl::make_transform(
      0.f, -1.f, 0.f, 0.f,
      0.f,  0.f, 1.f, 0.f,
      1.f,  0.f, 0.f, 0.f);
  // clang-format on
}

} // namespace anari_cycles
