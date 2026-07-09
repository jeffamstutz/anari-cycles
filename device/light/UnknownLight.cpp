// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "UnknownLight.h"
// cycles
#include "kernel/svm/types.h"
#include "scene/background.h"
#include "scene/camera.h"
#include "scene/shader.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "util/math_base.h"
#include "util/transform.h"
#include "util/types_float3.h"
// std
#include <cmath>

namespace anari_cycles {

UnknownLight::UnknownLight(std::string_view subtype, CyclesGlobalState *s)
    : Light(s, nullptr), m_subtype(subtype)
{
  reportMessage(ANARI_SEVERITY_WARNING,
      "created unknown %s object of subtype '%s'",
      anari::toString(ANARI_LIGHT),
      m_subtype.c_str());
}

bool UnknownLight::isValid() const
{
  return false;
}

void UnknownLight::warnIfUnknownObject() const
{
  reportMessage(ANARI_SEVERITY_WARNING,
      "encountered unknown %s object of subtype '%s'",
      anari::toString(ANARI_LIGHT),
      m_subtype.c_str());
}

math::mat4 UnknownLight::xfm() const
{
  return math::mat4(1.f);
}

} // namespace anari_cycles
