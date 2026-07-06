# Feature: KHR_LIGHT_RING

**Type:** spec completeness — easy Cycles mapping
**Depends on:** 01-fix-light-architecture.md, 02-fix-point-light.md
**Context:** `../IMPLEMENTATION_STATUS.md` §2.1 · registry: `khr_light_ring.json`

## Spec surface
`ring` subtype: `position`, `direction`, `openingAngle` (default π), `falloffAngle`,
`intensity`/`power`/`radiance`, `radius`, `innerRadius`, `intensityDistribution`
(see task 17), `c0`.

## Cycles mapping
`AreaLight` with `set_ellipse(true)` and `sizeu == sizev == 2*radius`
(cycles/src/scene/light.h:122-146); `spread` maps from openingAngle.
`innerRadius > 0` (annulus) has no direct Cycles socket — options: warn and ignore, or
emissive mesh disc with hole. Follow the QuadLight pattern (device/Light.cpp:443-507),
including the photometric conversions (radiance/intensity/power → strength by area).

## Acceptance
Ring light illuminates like an equivalent-area quad light (energy match test);
extension declared in cycles_device.json.

## Suggested skills
`/tdd`; `/verify`; `/code-review`.
