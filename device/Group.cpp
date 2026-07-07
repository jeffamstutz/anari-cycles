// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Group.h"
// cycles
#include "kernel/types.h"
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

void Group::addGroupToCurrentCyclesScene(const math::mat4 &xfm,
    const std::vector<ccl::Transform> *motion,
    uint32_t instanceId) const
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

  // The instance 'id' feeds the 'instanceId' frame channel through a
  // per-object float attribute read by the OutputAOV nodes in every surface
  // material (see Material::makeGraph()). Stored biased by +1 -- exact in a
  // float for ids < 2^24 -- so the "no id" default (~0u) becomes 0, the
  // value untouched AOV pixels read back anyway. Volumes get the attribute
  // too, but Cycles never runs AOV nodes in volume shading, so volume
  // pixels always read back ~0u.
  const float instanceIdAttr = instanceId == ~0u ? 0.f : instanceId + 1.f;
  auto setInstanceId = [&](ccl::Object *o) {
    o->attributes.emplace_back(
        OIIO::ustring("instanceId"), OIIO::TypeFloat, 1, &instanceIdAttr);
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
      setInstanceId(o);
      o->set_pass_id(s->id());
      // Ray visibility / compositing flags live on the ANARI Surface, so
      // every instance of a surface shares them (documented limitation of
      // CYCLES_SURFACE_COMPOSITING). set_visibility() no-ops at the default
      // mask (~0u).
      o->set_visibility(s->visibilityMask());
      o->set_use_holdout(s->holdout());
      o->set_is_shadow_catcher(s->shadowCatcher());
      // CYCLES_LIGHT_LINKING receiver/blocker set indices and the
      // CYCLES_LIGHTGROUPS emission routing; all setters no-op at their
      // defaults (0 / 0 / empty).
      o->set_receiver_light_set(s->receiverLightSet());
      o->set_blocker_shadow_set(s->blockerShadowSet());
      o->set_lightgroup(OIIO::ustring(s->lightGroup()));
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
      setInstanceId(o);
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
      auto makeLightObject = [&](ccl::Light *cl, const math::mat4 &lightXfm) {
        auto *o = state.scene->create_node<ccl::Object>();
        o->set_geometry(cl);
        o->set_tfm(mat4ToCycles(math::mul(xfm, lightXfm)));
        // On lights this flag only means "illuminates the shadow-catcher
        // sub-path" (the unshadowed reference a 'shadowCatcher' surface is
        // divided by). Blender sets it on every light by default; without it
        // the sub-path sees no light and catchers record no shadows.
        o->set_is_shadow_catcher(true);
        // KHR_AREA_LIGHTS 'visible': hide the light geometry from camera
        // rays (Cycles turns this into SHADER_EXCLUDE_CAMERA on the light);
        // illumination of the scene is unaffected.
        if (!l->visibleToCamera())
          o->set_visibility(o->get_visibility() & ~ccl::PATH_RAY_CAMERA);
        // CYCLES_LIGHT_LINKING: which receiver sets this light illuminates
        // and which blocker sets shadow it (default ~0 = all sets), plus the
        // CYCLES_LIGHTGROUPS pass its emission accumulates into. Setters
        // no-op at the defaults.
        o->set_light_set_membership(l->lightSetMembership());
        o->set_shadow_set_membership(l->shadowSetMembership());
        o->set_lightgroup(OIIO::ustring(l->lightGroup()));
      };
      makeLightObject(l->cyclesLight(), l->xfm());
      // Second emitter for e.g. two-sided quad lights.
      if (auto *second = l->secondaryCyclesLight())
        makeLightObject(second, l->secondaryXfm());
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
