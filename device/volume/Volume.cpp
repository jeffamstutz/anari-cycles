// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Volume.h"
// subtypes
#include "PrincipledVolume.h"
#include "TransferFunction1D.h"

namespace anari_cycles {

Volume::Volume(CyclesGlobalState *s) : Object(ANARI_VOLUME, s) {}

Volume::~Volume() = default;

uint32_t Volume::id() const
{
  return m_id;
}

Volume *Volume::createInstance(std::string_view subtype, CyclesGlobalState *s)
{
  if (subtype == "transferFunction1D")
    return new TransferFunction1D(s);
  else if (subtype == "principled")
    return new PrincipledVolume(s);
  else
    return (Volume *)new UnknownObject(ANARI_VOLUME, subtype, s);
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Volume *);
