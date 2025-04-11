// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Light.h"
#include "Surface.h"
#include "Volume.h"
#include "array/ObjectArray.h"

namespace anari_cycles {

struct Group : public Object {
  Group(CyclesGlobalState *s);
  ~Group() override;

  void commitParameters() override;

  void addGroupToCurrentCyclesScene(const ccl::Transform &xfm) const;

  box3 bounds() const override;

 private:
  helium::ChangeObserverPtr<ObjectArray> m_surfaceData;
  helium::ChangeObserverPtr<ObjectArray> m_volumeData;
  helium::ChangeObserverPtr<ObjectArray> m_lightData;
};

}  // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Group *, ANARI_WORLD);

