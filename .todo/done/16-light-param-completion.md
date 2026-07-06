# Feature: complete light parameters (power/radiance variants, angularDiameter, visible, layout, KHR_AREA_LIGHTS)

**Type:** spec completeness
**Depends on:** 01-fix-light-architecture.md, 02-fix-point-light.md
**Context:** `../IMPLEMENTATION_STATUS.md` §2.2 · registry: `khr_light_*.json`, `khr_area_lights.json`

## Gaps (device/Light.cpp)
- **point** (:379-385): missing `power`; **spot** (:409-419): missing `power`.
  (Quad already handles radiance/intensity/power precedence — reuse that logic, :447-465.)
- **directional** (:217-225): missing `radiance` and `angularDiameter`
  (Cycles `SunLight::angle`, cycles/src/scene/light.h:149-164 — direct mapping).
- **hdri** (:256-266): `visible` read but never applied; `layout` missing (only
  equirectangular defined — validate and warn on others).
- **KHR_AREA_LIGHTS**: adds `radiance` + `visible` to all subtypes and `radius` on point,
  `angularDiameter` on directional. Cycles: light camera-visibility via object visibility
  flags (`PATH_RAY_CAMERA`, cycles/src/scene/light.cpp `light_object_visibility_flags`);
  point `radius` partially plumbed already.
- quad `side="both"` unsupported (warns, :469-471) — Cycles area lights can be two-sided
  via `spread`/normal tricks or two lights back-to-back; decide and document.

## Acceptance
Photometric semantics verified numerically for each intensity quantity (e.g. power vs
intensity on a unit-distance floor patch); `visible=false` lights don't appear in camera
rays but still illuminate; extensions declared.

## Suggested skills
`/tdd` (photometric assertions); `/verify`; `/code-review`.
