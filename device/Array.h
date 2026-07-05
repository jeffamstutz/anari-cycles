// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/array/Array1D.h"
#include "helium/array/Array2D.h"
#include "helium/array/Array3D.h"
#include "helium/array/ObjectArray.h"

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

using Array2DMemoryDescriptor = helium::Array2DMemoryDescriptor;
using Array2D = helium::Array2D;

using Array3DMemoryDescriptor = helium::Array3DMemoryDescriptor;
using Array3D = helium::Array3D;

} // namespace anari_cycles

HELIUM_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Array1D *, ANARI_ARRAY1D);
HELIUM_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::ObjectArray *, ANARI_ARRAY1D);
