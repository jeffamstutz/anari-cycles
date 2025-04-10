// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Group.h"
// cycles
#include "scene/object.h"

namespace cycles {

Group::Group(CyclesGlobalState *s)
    : Object(ANARI_GROUP, s),
      m_surfaceData(this),
      m_volumeData(this),
      m_lightData(this)
{}

Group::~Group() = default;

void Group::commitParameters()
{
  m_surfaceData = getParamObject<ObjectArray>("surface");
  m_volumeData = getParamObject<ObjectArray>("volume");
  m_lightData = getParamObject<ObjectArray>("light");
}

void Group::addGroupToCurrentCyclesScene(const ccl::Transform &xfm) const
{
  auto &state = *deviceState();

  if (m_surfaceData) {
    auto **surfacesBegin = (Surface **)m_surfaceData->handlesBegin();
    auto **surfacesEnd = (Surface **)m_surfaceData->handlesEnd();

    std::for_each(surfacesBegin, surfacesEnd, [&](Surface *s) {
      if (!s->isValid()) {
        s->warnIfUnknownObject();
        return;
      }
      auto *o = state.scene->create_node<ccl::Object>();
      o->set_geometry(s->cyclesGeometry());
      o->set_tfm(xfm);
    });
  }

#if 0
  if (m_volumeData) {
    auto **volumesBegin = (Volume **)m_volumeData->handlesBegin();
    auto **volumesEnd = (Volume **)m_volumeData->handlesEnd();

    std::for_each(volumesBegin, volumesEnd, [&](Volume *v) {
      if (!v->isValid()) {
        v->warnIfUnknownObject();
        return;
      }
      auto *o = state.scene->create_node<ccl::Object>();
      o->set_geometry(v->cyclesGeometry());
      o->set_tfm(xfm);
    });
  }
#endif

  if (m_lightData) {
    auto **lightsBegin = (Light **)m_lightData->handlesBegin();
    auto **lightsEnd = (Light **)m_lightData->handlesEnd();

    std::for_each(lightsBegin, lightsEnd, [&](Light *l) {
      if (!l->isValid()) {
        l->warnIfUnknownObject();
        return;
      }
      auto *o = state.scene->create_node<ccl::Object>();
      o->set_geometry(l->cyclesLight());
      o->set_tfm(xfm);
    });
  }
}

box3 Group::bounds() const
{
  box3 b = empty_box3();
  if (m_surfaceData) {
    auto **surfacesBegin = (Surface **)m_surfaceData->handlesBegin();
    auto **surfacesEnd = (Surface **)m_surfaceData->handlesEnd();

    std::for_each(surfacesBegin, surfacesEnd, [&](Surface *s) {
      if (s->isValid())
        extend(b, s->geometry()->bounds());
      else
        s->warnIfUnknownObject();
    });
  }
  if (m_volumeData) {
    auto **volumesBegin = (Volume **)m_volumeData->handlesBegin();
    auto **volumesEnd = (Volume **)m_volumeData->handlesEnd();

    std::for_each(volumesBegin, volumesEnd, [&](Volume *s) {
      if (s->isValid())
        extend(b, s->bounds());
      else
        s->warnIfUnknownObject();
    });
  }
  return b;
}

} // namespace cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(cycles::Group *);
