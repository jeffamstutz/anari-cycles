// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "spatial_field/SpatialField.h"

namespace anari_cycles {

struct Volume : public Object
{
  Volume(CyclesGlobalState *s);
  ~Volume() override;

  static Volume *createInstance(std::string_view subtype, CyclesGlobalState *s);

  // Scene-owned geometry node rendered for this volume (may be null when the
  // volume is invalid).
  virtual ccl::Geometry *cyclesGeometry() const = 0;
  virtual box3 bounds() const = 0;

  // User id reported through the 'channel.objectId' frame channel.
  uint32_t id() const;

 protected:
  uint32_t m_id{~0u};
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Volume *, ANARI_VOLUME);
