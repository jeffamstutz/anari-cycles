// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Light.h"
#include "Surface.h"
#include "Volume.h"
#include "array/ObjectArray.h"

namespace cycles {

struct Group : public Object {
  Group(CyclesGlobalState *s);
  ~Group() override;

  void commitParameters() override;

  void addGroupToCurrentWorld(const ccl::Transform &xfm) const;

  box3 bounds() const override;

 private:
  helium::ChangeObserverPtr<ObjectArray> m_surfaceData;
  helium::ChangeObserverPtr<ObjectArray> m_volumeData;
  helium::ChangeObserverPtr<ObjectArray> m_lightData;
};

}  // namespace cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(cycles::Group *, ANARI_WORLD);

