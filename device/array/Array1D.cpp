// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Array1D.h"

namespace anari_cycles {

// Translate the spec 'region' parameter into the 'begin'/'end' parameters
// helium's 1D array commit reads (see Array1D.h). When 'region' is unset the
// translated parameters are removed so the array reverts to full capacity.
static void translateRegionParam(helium::Array &a)
{
  uint64_t region[2] = {0, 0};
  if (a.getParam("region", ANARI_UINT64_REGION1, region)) {
    a.setParam("begin", size_t(region[0]));
    a.setParam("end", size_t(region[1]));
  } else {
    a.removeParam("begin");
    a.removeParam("end");
  }
}

Array1D::Array1D(
    helium::BaseGlobalDeviceState *state, const Array1DMemoryDescriptor &d)
    : helium::Array1D(state, d)
{}

void Array1D::commitParameters()
{
  translateRegionParam(*this);
  helium::Array1D::commitParameters();
}

} // namespace anari_cycles

HELIUM_ANARI_TYPEFOR_DEFINITION(anari_cycles::Array1D *);
