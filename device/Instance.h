// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Group.h"

namespace anari_cycles {

struct Instance : public Object
{
  Instance(CyclesGlobalState *s);
  ~Instance() override;

  void commitParameters() override;

  Group *group() const;

  void addInstanceObjectsToCyclesScene();

  box3 bounds() const override;

  bool isValid() const override;

 private:
  helium::IntrusivePtr<Group> m_group;
  helium::ChangeObserverPtr<Array1D> m_xfmArray;
  math::mat4 m_xfm;
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Instance *, ANARI_INSTANCE);
