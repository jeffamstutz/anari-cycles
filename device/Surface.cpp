// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Surface.h"
// cycles
#include "kernel/types.h"

namespace anari_cycles {

Surface::Surface(CyclesGlobalState *s)
    : Object(ANARI_SURFACE, s), m_geometry(this)
{}

Surface::~Surface()
{
  cleanupCyclesNode();
}

void Surface::commitParameters()
{
  auto *prevGeometry = m_geometry.get();
  auto *prevMaterial = m_material.ptr;
  m_geometry = getParamObject<Geometry>("geometry");
  m_material = getParamObject<Material>("material");
  m_id = getParam<uint32_t>("id", ~0u);
  m_geometryHandleChanged = prevGeometry != m_geometry.get();
  m_materialHandleChanged = prevMaterial != m_material.ptr;

  // Core 'visible' plus the CYCLES_SURFACE_COMPOSITING per-ray-type flags.
  // Each 'visible.*' flag defaults to 'visible', so visible=false hides the
  // surface from every ray type while e.g. visible.shadow=true keeps its
  // shadows. Only the named PATH_RAY_* bits are raised (mirroring Blender's
  // object_ray_visibility()): ray/object visibility is an AND-nonzero test,
  // so stray extra bits (e.g. PATH_RAY_TRANSPARENT, carried by continuation
  // rays alongside their original type) must stay clear or a "fully
  // invisible" object would still be hit. Fully-visible surfaces keep the
  // Cycles socket default (~0u) so the setter no-ops.
  const bool visible = getParam<bool>("visible", true);
  const bool visCamera = getParam<bool>("visible.camera", visible);
  const bool visDiffuse = getParam<bool>("visible.diffuse", visible);
  const bool visGlossy = getParam<bool>("visible.glossy", visible);
  const bool visTransmission = getParam<bool>("visible.transmission", visible);
  const bool visShadow = getParam<bool>("visible.shadow", visible);
  const bool visVolumeScatter =
      getParam<bool>("visible.volumeScatter", visible);
  if (visCamera && visDiffuse && visGlossy && visTransmission && visShadow
      && visVolumeScatter) {
    m_visibilityMask = ~0u;
  } else {
    uint32_t mask = 0;
    mask |= visCamera ? ccl::PATH_RAY_CAMERA : 0;
    mask |= visDiffuse ? ccl::PATH_RAY_DIFFUSE : 0;
    // sharp specular rays carry GLOSSY|SINGULAR, so raise both
    mask |= visGlossy ? (ccl::PATH_RAY_GLOSSY | ccl::PATH_RAY_SINGULAR) : 0;
    mask |= visTransmission ? ccl::PATH_RAY_TRANSMIT : 0;
    mask |= visShadow ? ccl::PATH_RAY_SHADOW : 0;
    mask |= visVolumeScatter ? ccl::PATH_RAY_VOLUME_SCATTER : 0;
    m_visibilityMask = mask;
  }

  m_holdout = getParam<bool>("holdout", false);
  m_shadowCatcher = getParam<bool>("shadowCatcher", false);

  // CYCLES_LIGHT_LINKING: 'receiverLightSet' names the light-link set this
  // surface receives light from (it is then only lit by lights whose
  // 'lightSet' names that set, plus all unlinked lights); 'shadowBlockerSet'
  // names the blocker set this surface belongs to (it then only blocks
  // lights whose 'shadowSet' names that set, plus all unlinked lights).
  // Baked onto this surface's per-instance ccl::Objects at world-rebuild
  // time (Group::addGroupToCurrentCyclesScene()).
  auto *state = deviceState();
  m_receiverLightSet =
      getLinkSetIndexParam("receiverLightSet", state->lightLinkSets);
  m_blockerShadowSet =
      getLinkSetIndexParam("shadowBlockerSet", state->shadowLinkSets);
  // CYCLES_LIGHTGROUPS: emission from this surface's material lands in the
  // named lightgroup channel.
  m_lightGroup = getParamString("lightGroup", "");
}

uint32_t Surface::getLinkSetIndexParam(
    const char *name, CyclesGlobalState::LinkSetRegistry &reg)
{
  const std::string setName = getParamString(name, "");
  if (setName.empty())
    return 0;
  const int idx = reg.resolve(setName);
  if (idx < 0) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'%s' value '%s' ignored -- Cycles supports at most 63 distinct "
        "link-set names per namespace",
        name,
        setName.c_str());
    return 0;
  }
  return uint32_t(idx);
}

void Surface::finalize()
{
  auto *state = deviceState();

  if (m_geometryHandleChanged) {
    cleanupCyclesNode();
    if (m_geometry && m_geometry->isValid())
      m_cyclesGeometryNode = m_geometry->createCyclesGeometryNode();
  }

  if (isValid()) {
    m_geometry->syncCyclesNode(m_cyclesGeometryNode);
    if (m_materialHandleChanged || m_geometryHandleChanged) {
      ccl::array<ccl::Node *> used_shaders;
      used_shaders.push_back_slow(m_material->cyclesShader());
      m_cyclesGeometryNode->set_used_shaders(used_shaders);
      m_warnedPointInteriorVolume = false;
    }
    m_cyclesGeometryNode->tag_update(state->scene, true);

    // Known limitation: Cycles point primitives (sphere geometry) are always
    // backface-culled in the kernel (point_intersect_test), so rays inside a
    // sphere never hit its far side and the volume stack cannot record an
    // exit event. Interior absorption (PBR thickness/attenuationColor/
    // attenuationDistance) therefore has no effect inside point spheres
    // (only rim artifacts appear); mesh-backed geometry is unaffected.
    // Best effort: material-only re-commits do not re-run this finalize, so
    // a material that gains an interior volume later warns only once the
    // surface (or its geometry) is touched again.
    if (!m_warnedPointInteriorVolume && m_cyclesGeometryNode->is_pointcloud()
        && m_material->hasInteriorVolume()) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "material interior absorption (thickness/attenuation*) is not "
          "supported on 'sphere' geometry: Cycles point primitives are "
          "backface-culled and cannot form a closed volume");
      m_warnedPointInteriorVolume = true;
    }
  }

  m_geometryHandleChanged = false;
  m_materialHandleChanged = false;

  state->objectUpdates.lastSceneChange = helium::newTimeStamp();

  Object::finalize();
}

const Geometry *Surface::geometry() const
{
  return m_geometry.get();
}

const Material *Surface::material() const
{
  return m_material.ptr;
}

uint32_t Surface::id() const
{
  return m_id;
}

uint32_t Surface::visibilityMask() const
{
  return m_visibilityMask;
}

bool Surface::holdout() const
{
  return m_holdout;
}

bool Surface::shadowCatcher() const
{
  return m_shadowCatcher;
}

uint32_t Surface::receiverLightSet() const
{
  return m_receiverLightSet;
}

uint32_t Surface::blockerShadowSet() const
{
  return m_blockerShadowSet;
}

const std::string &Surface::lightGroup() const
{
  return m_lightGroup;
}

ccl::Geometry *Surface::cyclesGeometry() const
{
  return m_cyclesGeometryNode;
}

bool Surface::isValid() const
{
  return m_geometry && m_geometry->isValid() && m_material
      && m_material->isValid();
}

void Surface::warnIfUnknownObject() const
{
  if (m_geometry)
    m_geometry->warnIfUnknownObject();
  if (m_material)
    m_material->warnIfUnknownObject();
}

void Surface::cleanupCyclesNode()
{
  auto &state = *deviceState();
  // Reached from ~Surface() on object release, which can happen while the
  // render thread reads the scene (reentrant when called during finalize).
  // Deletion is deferred: scene->objects may still reference the node.
  CyclesGlobalState::SceneLock sceneLock(state);
  state.retireGeometry(cyclesGeometry());
  m_cyclesGeometryNode = nullptr;
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Surface *);
