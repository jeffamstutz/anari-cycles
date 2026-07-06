# Vendor ext: per-surface visibility flags, holdout, shadow catcher

**Type:** vendor extension — compositing workflows
**Context:** `../IMPLEMENTATION_STATUS.md` §4.2 · Cycles: `cycles/src/scene/object.h:37-76`

## Candidate surface/volume parameters
- Ray-visibility flags: camera / diffuse / glossy / transmission / shadow / volumeScatter →
  `Object::visibility` bitmask (`PATH_RAY_*`, cycles/src/kernel/types.h:142-161). This also
  delivers the *core spec* `visible` param on Surface (bool = all flags), which the audit
  found missing.
- `holdout` (bool) → `Object::use_holdout` (object.h:54).
- `shadowCatcher` (bool) → `Object::is_shadow_catcher` (object.h:55); pair with the
  shadow-catcher frame channels in task 29 (`PASS_SHADOW_CATCHER`, `_MATTE`) and Film's
  `use_approximate_shadow_catcher` (film.h:51).
- Optional: shadow-terminator offsets (object.h:56-57), per-object `ao_distance`
  (object.h:70), caustics caster/receiver (object.h:59-60).

## Plumbing note
Surfaces map to `ccl::Object` in `Group::addGroupToCurrentCyclesScene`
(device/Group.cpp:41-45) — params belong on ANARI Surface (device/Surface.cpp:17-26) and
get applied where the Object is created. Instanced surfaces share the ANARI Surface, so
flags apply to every instance (document this).

## Acceptance
`visible=false` surface disappears but still casts shadows when shadow flag on;
shadow catcher demo: floor catches sphere shadow over transparent background.

## Suggested skills
`/tdd`; `/verify`; `/code-review`.
