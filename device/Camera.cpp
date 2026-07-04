// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Camera.h"
// cycles
#include "scene/camera.h"
// std
#include <algorithm>
#include <limits>

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
  ccl::Transform matrixCorrection() const override;
  bool usesNativeStereo() const override;
};

// Camera definitions /////////////////////////////////////////////////////////

Camera::Camera(CyclesGlobalState *s)
    : Object(ANARI_CAMERA, s),
      m_motionTransform(this),
      m_motionScale(this),
      m_motionRotation(this),
      m_motionTranslation(this)
{}

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

  // KHR_CAMERA_SHUTTER -- the interval within the frame time domain [0,1]
  // during which the shutter is open. The default [0.5,0.5] (and any [t,t])
  // is a degenerate interval: no motion blur.
  m_shutter = getParam<helium::box1>("shutter", helium::box1{0.5f, 0.5f});
  if (m_shutter.upper < m_shutter.lower) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "invalid 'shutter' interval [%f, %f] (upper < lower) -- treating as "
        "a degenerate interval (no motion blur)",
        m_shutter.lower,
        m_shutter.upper);
  }

  // KHR_CAMERA_ROLLING_SHUTTER -- Cycles implements exactly one rolling
  // shutter model, ROLLING_SHUTTER_TOP: scan-lines are acquired top of the
  // image first (kernel time = 1 - y/height with raster y = 0 at the bottom
  // row, which is also the first row of the ANARI frame buffer -- this device
  // performs no vertical flip). Scanning the top row first means the shutter
  // sweeps downward across the image, i.e. ANARI direction 'down'. The other
  // directions have no Cycles equivalent and are ignored with a warning.
  auto rollingDir = getParamString("rollingShutterDirection", "none");
  m_rollingShutterDown = (rollingDir == "down");
  if (!m_rollingShutterDown && rollingDir != "none") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "unsupported 'rollingShutterDirection' value '%s' -- Cycles only "
        "supports a top-to-bottom scan ('down'); using 'none'",
        rollingDir.c_str());
  }
  m_rollingShutterDuration = getParam<float>("rollingShutterDuration", 0.f);
  if (m_rollingShutterDown && !(m_shutter.upper > m_shutter.lower)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'rollingShutterDirection' is set but the 'shutter' interval is "
        "degenerate -- rolling shutter has no effect without an open shutter");
  }

  // KHR_CAMERA_STEREO -- 'left'/'right' render a single eye. 'sideBySide'
  // and 'topBottom' require two renders (or per-eye imageRegion support)
  // composited into one frame, which this device cannot do in a single
  // Cycles render pass yet; they warn and fall back to 'none'.
  auto stereoMode = getParamString("stereoMode", "none");
  if (stereoMode == "left")
    m_stereoMode = StereoMode::LEFT;
  else if (stereoMode == "right")
    m_stereoMode = StereoMode::RIGHT;
  else {
    if (stereoMode != "none") {
      reportMessage(ANARI_SEVERITY_WARNING,
          "unsupported 'stereoMode' value '%s' -- only 'left' and 'right' "
          "are implemented (per-eye layouts need two render passes); "
          "using 'none'",
          stereoMode.c_str());
    }
    m_stereoMode = StereoMode::NONE;
  }
  m_interpupillaryDistance = getParam<float>("interpupillaryDistance", 0.0635f);

  // KHR_CAMERA_MOTION_TRANSFORMATION -- a time-varying camera-to-world
  // transform that overrides 'position'/'direction'/'up'. 'motion.transform'
  // keys take precedence over the scale/rotation/translation key arrays.
  m_motionTransform = getParamObject<Array1D>("motion.transform");
  m_motionScale = getParamObject<Array1D>("motion.scale");
  m_motionRotation = getParamObject<Array1D>("motion.rotation");
  m_motionTranslation = getParamObject<Array1D>("motion.translation");
  m_motion = MotionTrack();
  m_motion.matrix = readMotionKeys<math::mat4>(
      this, m_motionTransform.get(), ANARI_FLOAT32_MAT4, "motion.transform");
  if (m_motion.matrix.empty()) {
    m_motion.scale = readMotionKeys<math::float3>(
        this, m_motionScale.get(), ANARI_FLOAT32_VEC3, "motion.scale");
    m_motion.rotation = readMotionKeys<math::float4>(this,
        m_motionRotation.get(),
        ANARI_FLOAT32_QUAT_IJKW,
        "motion.rotation");
    m_motion.translation = readMotionKeys<math::float3>(this,
        m_motionTranslation.get(),
        ANARI_FLOAT32_VEC3,
        "motion.translation");
  }
  m_motion.time = getParam<helium::box1>("time", helium::box1{0.f, 1.f});
  if (!m_motion.empty() && m_motion.time.upper < m_motion.time.lower) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "invalid 'time' interval [%f, %f] (upper < lower) -- all motion "
        "keys collapse to the first key",
        m_motion.time.lower,
        m_motion.time.upper);
  }
}

void Camera::finalize()
{
  Object::finalize();
}

helium::box1 Camera::shutter() const
{
  if (m_shutter.upper < m_shutter.lower)
    return helium::box1{m_shutter.lower, m_shutter.lower};
  return m_shutter;
}

void Camera::setCameraCurrent(int width, int height)
{
  auto &state = *deviceState();

  // KHR_CAMERA_MOTION_TRANSFORMATION: bake the motion track onto the shutter
  // interval (same resampling contract as instance motion, MotionTrack.h).
  // Each baked key maps the canonical camera frame -- origin, direction
  // (0,0,-1), up (0,1,0) -- to world space, so the world matrix for step i is
  // key_i * poseMatrix(canonical); poseMatrix() keeps the stereo eye offset
  // and any subtype correction inside the moving frame. A degenerate shutter
  // or a track that does not move collapses to a static pose at the shutter
  // start (MOTION_POSITION_START contract), leaving motion blur off.
  ccl::array<ccl::Transform> cameraMotion;
  ccl::Transform matrix;
  state.cameraHasMotion = false;
  if (m_motion.empty())
    matrix = getMatrix();
  else {
    const helium::box1 shutterInterval = shutter();
    const ccl::Transform canonical = poseMatrix(
        anari_vec::float3{0.f, 0.f, 0.f},
        anari_vec::float3{0.f, 0.f, -1.f},
        anari_vec::float3{0.f, 1.f, 0.f});
    const auto steps = bakeMotionOnShutter(m_motion, shutterInterval);
    if (steps.empty()) {
      matrix =
          mat4ToCycles(m_motion.sample(shutterInterval.lower)) * canonical;
    } else {
      cameraMotion.resize(steps.size());
      for (size_t i = 0; i < steps.size(); i++)
        cameraMotion[i] = steps[i] * canonical;
      matrix = cameraMotion[0]; // MOTION_POSITION_START: pose at shutter start
      state.cameraHasMotion = true;
    }
  }
  state.scene->camera->set_matrix(matrix);
  // Also clears stale motion from a previous frame when empty.
  state.scene->camera->set_motion(cameraMotion);
  state.syncIntegratorMotionBlur();

  state.scene->camera->set_full_width(width);
  state.scene->camera->set_full_height(height);
  state.scene->camera->set_aperturesize(std::max(m_apertureRadius, 0.f));
  state.scene->camera->set_focaldistance(m_focusDistance);
  state.scene->camera->set_blades(
      static_cast<unsigned int>(std::max(m_apertureBlades, 0)));
  state.scene->camera->set_bladesrotation(m_apertureRotation);

  // KHR_CAMERA_SHUTTER -> Cycles mapping (only active when motion exists:
  // instance/camera motion enables integrator motion_blur -- without it
  // Cycles forces kernel shuttertime to -1, i.e. all rays at time 0.5, so
  // these sockets are inert for static scenes). The contract, honored by all
  // motion-key baking (MotionTrack.h bakeMotionOnShutter()):
  //   * Cycles' kernel ray->time domain [0,1] (uniform via the default flat
  //     shutter curve) corresponds exactly to the ANARI shutter interval
  //     [s0,s1]; Cycles motion step i of N is sampled at ANARI frame time
  //     s0 + (s1-s0) * i/(N-1). Cycles does NOT rescale ray->time by
  //     shuttertime, so the interval position/extent live entirely in how
  //     the motion keys are baked.
  //   * shuttertime is set to the interval extent (s1-s0): it only gates
  //     blur and biases heterogeneous-volume shading times; a degenerate
  //     [t,t] interval (extent 0) must render without motion blur -- task 14
  //     must bake all motion samples at the single time t (or skip enabling
  //     motion for that frame) since Cycles still spreads ray->time over
  //     [0,1] whenever kernel shuttertime != -1.
  //   * motion_position START matches "ray time 0 == interval start" for the
  //     one kernel consumer (volume_shader.h time offset).
  const float shutterExtent = std::max(m_shutter.upper - m_shutter.lower, 0.f);
  state.scene->camera->set_shuttertime(shutterExtent);
  state.scene->camera->set_motion_position(ccl::MOTION_POSITION_START);

  // KHR_CAMERA_ROLLING_SHUTTER: Cycles' rolling_shutter_duration is the
  // per-scan-line exposure expressed as a fraction of shuttertime (1 = pure
  // motion blur, 0 = instantaneous lines / pure rolling), while ANARI's
  // rollingShutterDuration is the per-line open time in frame-time units,
  // hence the division by the shutter extent. With a degenerate shutter
  // there is no time span to roll across, so the effect is disabled.
  if (m_rollingShutterDown && shutterExtent > 0.f) {
    state.scene->camera->set_rolling_shutter_type(
        ccl::Camera::ROLLING_SHUTTER_TOP);
    state.scene->camera->set_rolling_shutter_duration(
        std::clamp(m_rollingShutterDuration / shutterExtent, 0.f, 1.f));
  } else {
    state.scene->camera->set_rolling_shutter_type(
        ccl::Camera::ROLLING_SHUTTER_NONE);
    state.scene->camera->set_rolling_shutter_duration(0.f);
  }

  // KHR_CAMERA_STEREO: reset Cycles' native stereo state here; getMatrix()
  // applies the eye offset manually for non-panorama subtypes (Cycles' own
  // stereo path runs spherical_stereo_transform in *world* space for
  // perspective cameras, assuming a Z-up world -- wrong for arbitrary ANARI
  // 'up'). Omnidirectional overrides this with native spherical stereo.
  state.scene->camera->set_stereo_eye(ccl::Camera::STEREO_NONE);
  state.scene->camera->set_use_spherical_stereo(false);
  state.scene->camera->set_interocular_distance(m_interpupillaryDistance);
  // Parallel stereo (ANARI has no convergence parameter); FLT_MAX is Cycles'
  // parallel-convergence sentinel (kernel/camera/projection.h).
  state.scene->camera->set_convergence_distance(
      std::numeric_limits<float>::max());

  state.scene->camera->need_flags_update = true;
  state.scene->camera->need_device_update = true;
}

bool Camera::usesNativeStereo() const
{
  return false;
}

float Camera::stereoEyeOffset() const
{
  if (usesNativeStereo() || m_stereoMode == StereoMode::NONE)
    return 0.f;
  // Matches Cycles' convention: left eye sits at -IPD/2 along the camera
  // right vector, right eye at +IPD/2.
  return (m_stereoMode == StereoMode::LEFT ? -0.5f : 0.5f)
      * m_interpupillaryDistance;
}

ccl::Transform Camera::poseMatrix(const anari_vec::float3 &position,
    const anari_vec::float3 &direction,
    const anari_vec::float3 &upvec) const
{
  ccl::Transform retval;

  auto dir =
      normalize(ccl::make_float3(direction[0], direction[1], direction[2]));
  auto pos = ccl::make_float3(position[0], position[1], position[2]);
  auto up = normalize(ccl::make_float3(upvec[0], upvec[1], upvec[2]));

  const auto s = ccl::normalize(ccl::cross(dir, up));
  const auto u = ccl::normalize(ccl::cross(s, dir));

  // KHR_CAMERA_STEREO ('left'/'right'): offset the eye along the camera
  // right vector; view direction is unchanged (parallel stereo).
  pos += s * stereoEyeOffset();

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
  return retval * matrixCorrection();
}

ccl::Transform Camera::getMatrix() const
{
  return poseMatrix(m_pos, m_dir, m_up);
}

ccl::Transform Camera::matrixCorrection() const
{
  return ccl::transform_identity();
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

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Camera *);
