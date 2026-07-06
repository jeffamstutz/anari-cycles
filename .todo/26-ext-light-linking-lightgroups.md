# Vendor ext: light linking / shadow linking / lightgroups

**Type:** vendor extension — production lighting workflows
**Depends on:** 01-fix-light-architecture.md, 02-fix-point-light.md
**Context:** `../IMPLEMENTATION_STATUS.md` §4.2 · Cycles: `cycles/src/scene/object.h:72-76,124-125`

## Cycles capability
- Light linking: `Object::receiver_light_set` / `light_set_membership`;
  shadow linking: `blocker_shadow_set` / `shadow_set_membership` (64 sets each).
- Lightgroups: `Object::lightgroup` string + per-lightgroup combined passes
  (`Film::update_lightgroups`, cycles/src/scene/film.h:76; `Pass::lightgroup`, pass.h:58).

## Proposed ANARI surface
- On Light: `lightGroup` (STRING), `linkSet` (membership id/uint64 mask).
- On Surface: `receiverLightSet`, `shadowBlockerSet` (masks or string-set names — strings
  are friendlier; device maintains name→set-index mapping, warn past 64 sets).
- Frame channels `channel.lightgroup.<name>` for per-group AOVs (coordinate with task 29).

## Design first
This one deserves a short design pass before code: naming, string-vs-mask, and whether
linking lives on Light, Surface, or both (Cycles puts membership on the light's Object and
receiving on the surface's Object).

## Acceptance
Two lights, two surfaces, each linked to one light — renders show exclusive illumination;
lightgroup channel contains only its light's contribution.

## Suggested skills
`/design-an-interface` or `/grilling` for the API shape; then `/tdd`, `/verify`, `/code-review`.
