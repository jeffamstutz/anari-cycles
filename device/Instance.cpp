// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Instance.h"
// std
#include <cstring>

namespace anari_cycles {

// Instance definitions ///////////////////////////////////////////////////////

Instance *Instance::createInstance(
    std::string_view subtype, CyclesGlobalState *s)
{
  if (subtype == "transform" || subtype == "motionTransform"
      || subtype == "motionScaleRotationTranslation")
    return new Instance(s, subtype);
  return (Instance *)new UnknownObject(ANARI_INSTANCE, subtype, s);
}

Instance::Instance(CyclesGlobalState *s, std::string_view subtype)
    : Object(ANARI_INSTANCE, s),
      m_xfmArray(this),
      m_motionTransform(this),
      m_motionScale(this),
      m_motionRotation(this),
      m_motionTranslation(this)
{
  if (subtype == "motionTransform")
    m_subtype = Subtype::MOTION_TRANSFORM;
  else if (subtype == "motionScaleRotationTranslation")
    m_subtype = Subtype::MOTION_SRT;
  else
    m_subtype = Subtype::TRANSFORM;
}

Instance::~Instance() = default;

void Instance::commitParameters()
{
  m_group = getParamObject<Group>("group");
  // KHR_INSTANCE_TRANSFORM_ARRAY only applies to the 'transform' subtype.
  m_xfmArray = m_subtype == Subtype::TRANSFORM
      ? getParamObject<Array1D>("transform")
      : nullptr;
  m_xfm = getParam<helium::mat4>("transform", linalg::identity);

  m_motionTransform = nullptr;
  m_motionScale = nullptr;
  m_motionRotation = nullptr;
  m_motionTranslation = nullptr;
  m_motion = MotionTrack();

  if (m_subtype == Subtype::MOTION_TRANSFORM) {
    m_motionTransform = getParamObject<Array1D>("motion.transform");
    m_motion.matrix = readMotionKeys<math::mat4>(this,
        m_motionTransform.get(),
        ANARI_FLOAT32_MAT4,
        "motion.transform");
  } else if (m_subtype == Subtype::MOTION_SRT) {
    m_motionScale = getParamObject<Array1D>("motion.scale");
    m_motionRotation = getParamObject<Array1D>("motion.rotation");
    m_motionTranslation = getParamObject<Array1D>("motion.translation");
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

  if (isMotionSubtype()) {
    m_motion.time = getParam<helium::box1>("time", helium::box1{0.f, 1.f});
    if (m_motion.time.upper < m_motion.time.lower) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "invalid 'time' interval [%f, %f] (upper < lower) -- all motion "
          "keys collapse to the first key",
          m_motion.time.lower,
          m_motion.time.upper);
    }
  }
}

Group *Instance::group() const
{
  return m_group.ptr;
}

bool Instance::isMotionSubtype() const
{
  return m_subtype != Subtype::TRANSFORM;
}

bool Instance::hasMotion() const
{
  return isMotionSubtype() && !m_motion.empty();
}

math::mat4 Instance::motionPoseAt(float t) const
{
  // Per the extension registry the static 'transform' is still present on
  // motion subtypes -- it is the pose used when no motion arrays are given.
  return m_motion.empty() ? m_xfm : m_motion.sample(t);
}

bool Instance::addInstanceObjectsToCyclesScene(const helium::box1 &shutter)
{
  if (!isValid())
    return false;

  if (!isMotionSubtype()) {
    if (!m_xfmArray)
      m_group->addGroupToCurrentCyclesScene(m_xfm);
    else {
      auto *begin = m_xfmArray->beginAs<helium::mat4>();
      auto *end = m_xfmArray->endAs<helium::mat4>();
      std::for_each(begin, end, [&](const helium::mat4 &m) {
        m_group->addGroupToCurrentCyclesScene(m);
      });
    }
    return false;
  }

  // Motion subtypes: resample the motion track onto the camera shutter
  // interval (see MotionTrack.h for the resampling contract). A degenerate
  // shutter, missing motion arrays, or a track that does not actually move
  // across the shutter all collapse to a single static pose at the shutter
  // start so no motion-blur machinery gets enabled.
  const auto steps = bakeMotionOnShutter(m_motion, shutter);
  if (steps.empty()) {
    m_group->addGroupToCurrentCyclesScene(motionPoseAt(shutter.lower));
    return false;
  }

  // MOTION_POSITION_START contract: the object's base transform is the pose
  // at the shutter start (== motion step 0).
  m_group->addGroupToCurrentCyclesScene(motionPoseAt(shutter.lower), &steps);
  return true;
}

box3 Instance::bounds() const
{
  box3 b = empty_box3();
  if (!isValid())
    return b;

  auto gb = m_group->bounds();
  if (gb.lower.x > gb.upper.x)
    return b;

  auto extendBounds = [&](const helium::mat4 &m) {
    auto xfm = mat4ToCycles(m);
    extend(b,
        ccl::transform_point(
            &xfm, make_float3(gb.lower.x, gb.lower.y, gb.lower.z)));
    extend(b,
        ccl::transform_point(
            &xfm, make_float3(gb.lower.x, gb.upper.y, gb.lower.z)));
    extend(b,
        ccl::transform_point(
            &xfm, make_float3(gb.upper.x, gb.upper.y, gb.lower.z)));
    extend(b,
        ccl::transform_point(
            &xfm, make_float3(gb.upper.x, gb.lower.y, gb.lower.z)));
    extend(b,
        ccl::transform_point(
            &xfm, make_float3(gb.lower.x, gb.lower.y, gb.upper.z)));
    extend(b,
        ccl::transform_point(
            &xfm, make_float3(gb.lower.x, gb.upper.y, gb.upper.z)));
    extend(b,
        ccl::transform_point(
            &xfm, make_float3(gb.upper.x, gb.upper.y, gb.upper.z)));
    extend(b,
        ccl::transform_point(
            &xfm, make_float3(gb.upper.x, gb.lower.y, gb.upper.z)));
  };

  if (isMotionSubtype()) {
    if (m_motion.empty())
      extendBounds(m_xfm);
    else {
      // Cover the whole time range of the track. Rotation (slerp between
      // quaternion keys AND the decomposed interpolation of matrix keys) can
      // bulge outside the key poses' hull, so sample intermediate times per
      // segment; only pure scale/translation tracks interpolate linearly
      // (extremes exactly at the keys).
      const size_t nKeys = std::max(m_motion.numKeys(), size_t(2));
      const bool linearOnly =
          m_motion.matrix.empty() && m_motion.rotation.empty();
      const size_t samplesPerSegment = linearOnly ? 1 : 8;
      const size_t n = (nKeys - 1) * samplesPerSegment + 1;
      const float extent = m_motion.time.upper - m_motion.time.lower;
      for (size_t i = 0; i < n; i++) {
        const float t =
            m_motion.time.lower + extent * float(i) / float(n - 1);
        extendBounds(m_motion.sample(t));
      }
    }
  } else if (!m_xfmArray)
    extendBounds(m_xfm);
  else {
    auto *begin = m_xfmArray->beginAs<helium::mat4>();
    auto *end = m_xfmArray->endAs<helium::mat4>();
    std::for_each(begin, end, [&](const helium::mat4 &m) { extendBounds(m); });
  }

  return b;
}

bool Instance::isValid() const
{
  return m_group;
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Instance *);
