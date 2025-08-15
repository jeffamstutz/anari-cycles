// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Sampler.h"

namespace anari_cycles {

Sampler::Sampler(CyclesGlobalState *s) : Object(ANARI_SAMPLER, s)
{
  // TODO
}

Sampler::~Sampler() = default;

Sampler *Sampler::createInstance(std::string_view subtype, CyclesGlobalState *s)
{
#if 0
  if (subtype == "image2D")
    return new StructuredRegularField(s);
  else
#endif
  return (Sampler *)new UnknownObject(ANARI_SAMPLER, subtype, s);
}

} // namespace anari_cycles
