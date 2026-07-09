// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Camera.h"

namespace anari_cycles {

struct Omnidirectional : public Camera
{
  Omnidirectional(CyclesGlobalState *s);

  void commitParameters() override;
  void setCameraCurrent(int width, int height) override;

 protected:
  ccl::Transform matrixCorrection() const override;
  bool usesNativeStereo() const override;
};

} // namespace anari_cycles
