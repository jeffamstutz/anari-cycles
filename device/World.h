// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Instance.h"

namespace anari_cycles {

struct World : public Object
{
  World(CyclesGlobalState *s);
  ~World() override;

  void commitParameters() override;
  void finalize() override;

  // (Re)build scene->objects. 'shutter' is the camera shutter interval that
  // motion instances bake their motion keys onto (see MotionTrack.h); it is
  // ignored by static instances, so worlds without motion instances need no
  // rebuild when the shutter changes (see hasMotionInstances()).
  void setCyclesWorldObjects(const helium::box1 &shutter);

  // 'true' when any committed instance carries motion keys -- i.e. the baked
  // scene objects depend on the camera shutter interval.
  bool hasMotionInstances() const;

  // 'true' when rendering with 'shutter' requires rebuilding the scene
  // objects because motion instances were baked against a different shutter
  // interval.
  bool motionRequiresRebake(const helium::box1 &shutter);

  // First background-type light (hdri/sky) in the world, also counting how
  // many there are in total (Cycles has a single background slot).
  Light *findFirstBackgroundLight(size_t &backgroundLightCount) const;

  // The background-type light (hdri/sky) currently driving
  // scene->background (cached by setupBackground() during
  // setCyclesWorldObjects()); nullptr when the world has none. Valid between
  // world rebuilds because the world's light arrays keep the light alive.
  Light *backgroundLight() const;

  box3 bounds() const override;

 private:
  void setupBackground();
  helium::ChangeObserverPtr<ObjectArray> m_zeroSurfaceData;
  helium::ChangeObserverPtr<ObjectArray> m_zeroLightData;
  helium::ChangeObserverPtr<ObjectArray> m_zeroVolumeData;
  helium::IntrusivePtr<Group> m_zeroGroup;
  helium::IntrusivePtr<Instance> m_zeroInstance;

  // Observed so committing a change on the instance array (new handles or a
  // new 'region' -- KHR_ARRAY1D_REGION) re-finalizes the world and rebuilds
  // the scene objects.
  helium::ChangeObserverPtr<ObjectArray> m_instanceData;

  helium::IntrusivePtr<Light> m_backgroundLight;

  // Camera shutter interval the motion instances were last baked against
  // (kept on the world -- not per frame -- so multiple frames/cameras
  // sharing this world detect a mismatch and trigger a rebake).
  helium::box1 m_bakedShutter{0.f, 0.f};
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::World *, ANARI_WORLD);
