// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Instance.h"
// std
#include <cstring>

namespace anari_cycles {

Instance::Instance(CyclesGlobalState *s) : Object(ANARI_INSTANCE, s) {}

Instance::~Instance() = default;

void Instance::commitParameters()
{
  m_group = getParamObject<Group>("group");
  m_xfm = getParam<helium::mat4>("transform", linalg::identity);
}

Group *Instance::group() const
{
  return m_group.ptr;
}

void Instance::addInstanceObjectsToCyclesScene()
{
  if (m_group)
    m_group->addGroupToCurrentCyclesScene(m_xfm);
}

box3 Instance::bounds() const
{
  box3 b = empty_box3();
  if (m_group) {
    auto gb = m_group->bounds();
    auto xfm = mat4ToCycles(m_xfm);
    extend(b,
        ccl::transform_point(
            &xfm, make_float3(gb.lower.x, gb.lower.y, gb.lower.z)));
    extend(b,
        ccl::transform_point(
            &xfm, make_float3(gb.lower.x, gb.upper.y, gb.lower.z)));
    extend(b,
        ccl::transform_point(
            &xfm, make_float3(gb.upper.x, gb.upper.y, gb.lower.z)));
    extend(b,
        ccl::transform_point(
            &xfm, make_float3(gb.upper.x, gb.lower.y, gb.lower.z)));
    extend(b,
        ccl::transform_point(
            &xfm, make_float3(gb.lower.x, gb.lower.y, gb.upper.z)));
    extend(b,
        ccl::transform_point(
            &xfm, make_float3(gb.lower.x, gb.upper.y, gb.upper.z)));
    extend(b,
        ccl::transform_point(
            &xfm, make_float3(gb.upper.x, gb.upper.y, gb.upper.z)));
    extend(b,
        ccl::transform_point(
            &xfm, make_float3(gb.upper.x, gb.lower.y, gb.upper.z)));
  }
  return b;
}

bool Instance::isValid() const
{
  return m_group;
}

} // namespace anari_cycles