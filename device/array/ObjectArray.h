// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Array1D.h"
// helium
#include "helium/array/ObjectArray.h"

namespace anari_cycles {

// Object arrays implement 'region' locally instead of translating it into
// helium's 'begin'/'end': helium::ObjectArray::handlesBegin() offsets by its
// internal begin even though updateInternalHandleArrays() (run on unmap)
// compacts the live-handle list to start at the region, so a non-zero begin
// plus a later map/unmap would index out of bounds. Keeping the base
// full-range and slicing in the (shadowing) handle accessors is correct for
// every map/commit order. All device-side consumers iterate
// handlesBegin()/handlesEnd() through this type; note the sliced
// handlesEnd() excludes appendHandle() entries (unused by this device).
struct ObjectArray : public helium::ObjectArray
{
  ObjectArray(
      helium::BaseGlobalDeviceState *state, const Array1DMemoryDescriptor &d);
  void commitParameters() override;

  helium::BaseObject **handlesBegin() const;
  helium::BaseObject **handlesEnd() const;

 private:
  size_t m_regionBegin{0};
  size_t m_regionEnd{0}; // == capacity when no 'region' is set
};

} // namespace anari_cycles

HELIUM_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::ObjectArray *, ANARI_ARRAY1D);
