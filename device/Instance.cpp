// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Instance.h"
// std
#include <cstring>

namespace anari_cycles {

Instance::Instance(CyclesGlobalState *s)
    : Object(ANARI_INSTANCE, s), m_xfmArray(this)
{}

Instance::~Instance() = default;

void Instance::commitParameters()
{
  m_group = getParamObject<Group>("group");
  m_xfmArray = getParamObject<Array1D>("transform");
  m_xfm = getParam<helium::mat4>("transform", linalg::identity);
}

Group *Instance::group() const
{
  return m_group.ptr;
}

void Instance::addInstanceObjectsToCyclesScene()
{
  if (!isValid())
    return;

  if (!m_xfmArray)
    m_group->addGroupToCurrentCyclesScene(m_xfm);
  else {
    auto *begin = m_xfmArray->beginAs<helium::mat4>();
    auto *end = m_xfmArray->endAs<helium::mat4>();
    std::for_each(begin, end, [&](const helium::mat4 &m) {
      m_group->addGroupToCurrentCyclesScene(m);
    });
  }
}

box3 Instance::bounds() const
{
  box3 b = empty_box3();
  if (!isValid())
    return b;

  auto gb = m_group->bounds();

  auto extendBounds = [&](const helium::mat4 &m) {
    auto xfm = mat4ToCycles(m);
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
  };

  if (!m_xfmArray)
    extendBounds(m_xfm);
  else {
    auto *begin = m_xfmArray->beginAs<helium::mat4>();
    auto *end = m_xfmArray->endAs<helium::mat4>();
    std::for_each(begin, end, [&](const helium::mat4 &m) { extendBounds(m); });
  }

  return b;
}

bool Instance::isValid() const
{
  return m_group;
}

} // namespace anari_cycles