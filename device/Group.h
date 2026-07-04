// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Light.h"
#include "Surface.h"
#include "Volume.h"
// std
#include <vector>

namespace anari_cycles {

struct Group : public Object
{
  Group(CyclesGlobalState *s);
  ~Group() override;

  void commitParameters() override;

  // Instantiate the group's contents as Cycles scene objects under 'xfm'.
  // 'motion', when given (size >= 2), is a per-object motion-transform array
  // uniformly spanning the camera shutter interval (Cycles kernel ray-time
  // [0,1]); it applies to surfaces and volumes -- Cycles has no motion blur
  // for lights, which use 'xfm' (the shutter-start pose) only.
  void addGroupToCurrentCyclesScene(const math::mat4 &xfm,
      const std::vector<ccl::Transform> *motion = nullptr) const;

  box3 bounds() const override;

  // Accessor for light data (needed for HDRI light discovery)
  const ObjectArray* lightData() const { return m_lightData.get(); }

 private:
  helium::ChangeObserverPtr<ObjectArray> m_surfaceData;
  helium::ChangeObserverPtr<ObjectArray> m_volumeData;
  helium::ChangeObserverPtr<ObjectArray> m_lightData;
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Group *, ANARI_GROUP);
