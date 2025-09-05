// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "SamplerImageLoader.h"
// std
#include <memory>
// cycles
#include "scene/image.h"

namespace anari_cycles {

struct Sampler : public Object
{
  Sampler(CyclesGlobalState *s);
  ~Sampler() override;

  static Sampler *createInstance(
      std::string_view subtype, CyclesGlobalState *s);

  virtual std::unique_ptr<SamplerImageLoader> makeCyclesImageLoader() const;
  virtual ccl::ImageParams makeCyclesImageParams() const;
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Sampler *, ANARI_SAMPLER);
