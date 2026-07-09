// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/array/Array1D.h"

namespace anari_cycles {

using Array = helium::Array;

using Array1DMemoryDescriptor = helium::Array1DMemoryDescriptor;

// KHR_ARRAY1D_REGION: the 'region' parameter (ANARI_UINT64_REGION1, i.e.
// {begin, end} element indices) restricts the valid region of a 1D array
// without re-creating it; consumers observe only [begin, end) through the
// region-aware accessors (begin()/end()/size()/totalSize()).
//
// helium already implements 1D sub-ranging, but through its own non-spec
// 'begin'/'end' size_t parameters -- these subclasses translate 'region'
// into them at commit time. 'region' is the only supported sub-range
// interface on this device; when it is absent the full capacity is used.
//
// Out-of-range values follow helium's clamping: begin is clamped to
// [0, capacity-1], end to [1, capacity], and a reversed region is swapped
// (with a warning). Consequence of the clamps: region {0,0} yields a
// 1-element region (end clamps up to 1), while {k,k} with k >= 1 is an
// empty region that is reported as an error and yields a zero-sized array.
struct Array1D : public helium::Array1D
{
  Array1D(
      helium::BaseGlobalDeviceState *state, const Array1DMemoryDescriptor &d);
  void commitParameters() override;
};

} // namespace anari_cycles

HELIUM_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Array1D *, ANARI_ARRAY1D);
