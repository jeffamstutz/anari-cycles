// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Instance.h"

namespace anari_cycles {

struct World : public Object
{
  World(CyclesGlobalState *s);
  ~World() override;

  bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint64_t size,
      uint32_t flags) override;

  void commitParameters() override;
  void finalize() override;

  void setCyclesWorldObjects();

  Light *findFirstHDRILight() const;

  box3 bounds() const override;

 private:
  void setupHDRIBackground();
  helium::ChangeObserverPtr<ObjectArray> m_zeroSurfaceData;
  helium::ChangeObserverPtr<ObjectArray> m_zeroLightData;
  helium::ChangeObserverPtr<ObjectArray> m_zeroVolumeData;
  helium::IntrusivePtr<Group> m_zeroGroup;
  helium::IntrusivePtr<Instance> m_zeroInstance;

  helium::IntrusivePtr<ObjectArray> m_instanceData;
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::World *, ANARI_WORLD);
