// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Group.h"
// cycles
#include "scene/object.h"

namespace anari_cycles {

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

void Group::addGroupToCurrentCyclesScene(
    const math::mat4 &xfm, const std::vector<ccl::Transform> *motion) const
{
  auto &state = *deviceState();

  auto cxfm = mat4ToCycles(xfm);

  // Object::set_motion() steals the array, so every object needs its own
  // copy of the baked motion steps.
  auto setMotion = [&](ccl::Object *o) {
    if (!motion || motion->size() < 2)
      return;
    ccl::array<ccl::Transform> steps;
    steps.resize(motion->size());
    std::copy(motion->begin(), motion->end(), steps.data());
    o->set_motion(steps);
  };

  if (m_surfaceData) {
    auto **surfacesBegin = (Surface **)m_surfaceData->handlesBegin();
    auto **surfacesEnd = (Surface **)m_surfaceData->handlesEnd();

    std::for_each(surfacesBegin, surfacesEnd, [&](Surface *s) {
      if (!s->isValid()) {
        s->warnIfUnknownObject();
        return;
      }
      if (!s->cyclesGeometry())
        return;
      auto *o = state.scene->create_node<ccl::Object>();
      o->set_geometry(s->cyclesGeometry());
      o->set_tfm(cxfm);
      setMotion(o);
      o->set_pass_id(s->id());
    });
  }

  if (m_volumeData) {
    auto **volumesBegin = (Volume **)m_volumeData->handlesBegin();
    auto **volumesEnd = (Volume **)m_volumeData->handlesEnd();

    std::for_each(volumesBegin, volumesEnd, [&](Volume *v) {
      if (!v->isValid()) {
        v->warnIfUnknownObject();
        return;
      }
      if (!v->cyclesGeometry())
        return;
      auto *o = state.scene->create_node<ccl::Object>();
      o->set_geometry(v->cyclesGeometry());
      o->set_tfm(cxfm);
      setMotion(o);
      o->set_pass_id(v->id());
    });
  }

  if (m_lightData) {
    auto **lightsBegin = (Light **)m_lightData->handlesBegin();
    auto **lightsEnd = (Light **)m_lightData->handlesEnd();

    if (motion && motion->size() > 1 && lightsBegin != lightsEnd) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "lights in a motion instance do not motion blur (unsupported by "
          "Cycles) -- placing them at the shutter-start pose");
    }

    std::for_each(lightsBegin, lightsEnd, [&](Light *l) {
      if (!l->isValid()) {
        l->warnIfUnknownObject();
        return;
      }
      auto *o = state.scene->create_node<ccl::Object>();
      o->set_geometry(l->cyclesLight());
      o->set_tfm(mat4ToCycles(math::mul(xfm, l->xfm())));
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

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Group *);
