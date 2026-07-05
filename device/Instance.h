// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Group.h"
#include "MotionTrack.h"

namespace anari_cycles {

struct Instance : public Object
{
  // Subtypes: 'transform' (static, KHR_INSTANCE_TRANSFORM[_ARRAY]),
  // 'motionTransform' (KHR_INSTANCE_MOTION_TRANSFORM) and
  // 'motionScaleRotationTranslation'
  // (KHR_INSTANCE_MOTION_SCALE_ROTATION_TRANSLATION). Unknown subtypes yield
  // an UnknownObject.
  static Instance *createInstance(
      std::string_view subtype, CyclesGlobalState *state);

  Instance(CyclesGlobalState *s, std::string_view subtype = "transform");
  ~Instance() override;

  void commitParameters() override;
  void finalize() override;

  Group *group() const;

  // 'true' when this instance carries motion keys that must be (re)baked
  // against the camera shutter interval.
  bool hasMotion() const;

  // Returns 'true' when motion steps were baked into the created objects
  // (the caller then enables integrator motion blur).
  bool addInstanceObjectsToCyclesScene(const helium::box1 &shutter);

  box3 bounds() const override;

  bool isValid() const override;

 private:
  bool isMotionSubtype() const;
  // The motion pose at absolute frame time 't' (falls back to the static
  // 'transform' parameter when no motion arrays are set).
  math::mat4 motionPoseAt(float t) const;

  helium::IntrusivePtr<Group> m_group;
  helium::ChangeObserverPtr<Array1D> m_xfmArray;
  math::mat4 m_xfm;

  // KHR_FRAME_CHANNEL_INSTANCE_ID: uniform 'id' (~0u = unset) plus the
  // per-transform 'id' array of KHR_INSTANCE_TRANSFORM_ARRAY.
  uint32_t m_id{~0u};
  helium::ChangeObserverPtr<Array1D> m_idArray;

  enum class Subtype
  {
    TRANSFORM,
    MOTION_TRANSFORM,
    MOTION_SRT
  };
  Subtype m_subtype{Subtype::TRANSFORM};

  // KHR_INSTANCE_MOTION_TRANSFORM / _SCALE_ROTATION_TRANSLATION
  helium::ChangeObserverPtr<Array1D> m_motionTransform;
  helium::ChangeObserverPtr<Array1D> m_motionScale;
  helium::ChangeObserverPtr<Array1D> m_motionRotation;
  helium::ChangeObserverPtr<Array1D> m_motionTranslation;
  helium::box1 m_time{0.f, 1.f};
  MotionTrack m_motion; // rebuilt from the arrays in finalize()
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Instance *, ANARI_INSTANCE);
