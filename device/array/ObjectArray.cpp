// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "ObjectArray.h"
// std
#include <algorithm>

namespace anari_cycles {

ObjectArray::ObjectArray(
    helium::BaseGlobalDeviceState *state, const Array1DMemoryDescriptor &d)
    : helium::ObjectArray(state, d), m_regionEnd(d.numItems)
{}

void ObjectArray::commitParameters()
{
  // The base stays full-range (see ObjectArray.h) -- make sure helium's
  // non-spec 'begin'/'end' params are absent so its commit keeps
  // begin=0/end=capacity.
  removeParam("begin");
  removeParam("end");
  helium::ObjectArray::commitParameters();

  // Same clamping as helium's 1D sub-range commit (documented in Array1D.h).
  const size_t cap = totalCapacity();
  m_regionBegin = 0;
  m_regionEnd = cap;
  uint64_t region[2] = {0, 0};
  if (cap > 0 && getParam("region", ANARI_UINT64_REGION1, region)) {
    size_t b = std::clamp(size_t(region[0]), size_t(0), cap - 1);
    size_t e = std::clamp(size_t(region[1]), size_t(1), cap);
    if (b > e) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "array 'region' begin is not less than end, swapping values");
      std::swap(b, e);
    }
    if (b == e) {
      reportMessage(
          ANARI_SEVERITY_ERROR, "array region size must be greater than zero");
    }
    m_regionBegin = b;
    m_regionEnd = e;
  }
}

helium::BaseObject **ObjectArray::handlesBegin() const
{
  // The base's begin is always 0 here, so its handlesBegin() is the start of
  // the full live-handle list regardless of map/unmap history.
  return helium::ObjectArray::handlesBegin() + m_regionBegin;
}

helium::BaseObject **ObjectArray::handlesEnd() const
{
  return helium::ObjectArray::handlesBegin() + m_regionEnd;
}

} // namespace anari_cycles

HELIUM_ANARI_TYPEFOR_DEFINITION(anari_cycles::ObjectArray *);
