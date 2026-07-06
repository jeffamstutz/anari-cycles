# Fix: attribute-driven material color renders black

**Type:** P0 bugfix (verified)
**Context:** `../IMPLEMENTATION_STATUS.md` §1.4 · setup in `README.md`

## Problem
Scenes whose material color is sourced from geometry attributes render black:
`anariRenderTests` `demo_cornell_box` (fully black at 64 spp), `test_triangle_attributes`,
`test_pbr_spheres` (black + red fireflies, possibly NaNs). Constant-color and
texture-sampler materials render fine (`test_textured_cube`).

## Where to look
- Material graph attribute wiring: device/Material.cpp:394-421 (`AttributeNode` creation;
  supports `color`, `attribute0..3` names).
- Geometry attribute upload: device/Geometry.cpp:13-26, 60-88 (convert_toFloat4 path),
  per-subtype attribute storage (sphere drops 4th component, Geometry.cpp:519-561).
- Likely a name mismatch between the Cycles attribute names geometry creates and what the
  material's `AttributeNode` requests (e.g. `vertex.color` → `ATTR_STD_VERTEX_COLOR` vs a
  custom ustring). Compare against how Cycles names standard attributes
  (cycles/src/scene/attribute.h, kernel/types.h `AttributeStandard`).

## Goal
`color`/`attribute0..3` bindings on matte and physicallyBased sample the correct geometry
attribute for triangle, quad, and sphere. Chase the red fireflies afterwards (may resolve
itself, else see sample clamping in task 24).

## Acceptance
- Cornell box renders recognizably (red/green walls, white floor) — requires task 01 too.
- `test_triangle_attributes` shows the attribute gradients.
- `test_pbr_spheres` shows colored spheres varying in roughness/metallic.

## Suggested skills
`/diagnosing-bugs`; `/verify` (visually check the three render tests); `/code-review`.
