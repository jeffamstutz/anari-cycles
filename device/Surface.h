// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"
#include "Material.h"
// cycles
#include "scene/geometry.h"

namespace anari_cycles {

struct Surface : public Object
{
  Surface(CyclesGlobalState *s);
  ~Surface() override;

  void commitParameters() override;
  void finalize() override;

  const Geometry *geometry() const;
  const Material *material() const;
  uint32_t id() const;

  // Cycles ray-visibility bitmask (PATH_RAY_*) from the core 'visible'
  // parameter and the CYCLES_SURFACE_COMPOSITING per-ray-type flags.
  uint32_t visibilityMask() const;
  bool holdout() const;
  bool shadowCatcher() const;

  // CYCLES_LIGHT_LINKING: link-set indices for this surface's scene objects
  // (Object::receiver_light_set / blocker_shadow_set). 0 -- the default set
  // -- when the 'receiverLightSet'/'shadowBlockerSet' parameter is unset.
  uint32_t receiverLightSet() const;
  uint32_t blockerShadowSet() const;

  // CYCLES_LIGHTGROUPS: lightgroup AOV receiving this surface's emission
  // (Object::lightgroup); empty when unset.
  const std::string &lightGroup() const;

  ccl::Geometry *cyclesGeometry() const;

  bool isValid() const override;
  void warnIfUnknownObject() const override;

 private:
  void cleanupCyclesNode();

  // Resolve a 'receiverLightSet'/'shadowBlockerSet' set-name parameter into
  // a link-set index via the device-wide registry; 0 (the default set) when
  // unset or when more than 63 distinct names exist (warned).
  uint32_t getLinkSetIndexParam(
      const char *name, CyclesGlobalState::LinkSetRegistry &reg);

  helium::ChangeObserverPtr<Geometry> m_geometry;
  helium::IntrusivePtr<Material> m_material;

  ccl::Geometry *m_cyclesGeometryNode{nullptr};
  uint32_t m_id{~0u};
  uint32_t m_visibilityMask{~0u};
  bool m_holdout{false};
  bool m_shadowCatcher{false};
  uint32_t m_receiverLightSet{0};
  uint32_t m_blockerShadowSet{0};
  std::string m_lightGroup;
  bool m_geometryHandleChanged{false};
  bool m_materialHandleChanged{false};
  // one warning per geometry/material pairing (see finalize())
  bool m_warnedPointInteriorVolume{false};
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Surface *, ANARI_SURFACE);
