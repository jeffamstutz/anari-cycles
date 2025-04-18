// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "CyclesGlobalState.h"
#include "Frame.h"

namespace anari_cycles {

CyclesGlobalState::CyclesGlobalState(ANARIDevice d)
    : helium::BaseGlobalDeviceState(d)
{}

void CyclesGlobalState::waitOnCurrentFrame() const
{
  output_driver->wait();
}

} // namespace anari_cycles
