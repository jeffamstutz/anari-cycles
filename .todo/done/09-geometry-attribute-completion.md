# Feature: complete geometry attribute surface (primitive.*, faceVarying.*, tangent, index types)

**Type:** spec completeness
**Depends on:** 03-fix-attribute-color-plumbing.md
**Context:** `../IMPLEMENTATION_STATUS.md` §2.2 · registry: `khr_geometry_triangle.json` etc.

## Gaps (per code audit of device/Geometry.cpp)
- `primitive.color`, `primitive.attribute0..3`, `primitive.id` — not read on any subtype.
  (Cycles: per-face/per-curve/per-point attributes via `ATTR_ELEMENT_FACE` etc.)
- `faceVarying.normal/tangent/color/attribute0..3` (triangle/quad) — not read.
  (Cycles: `ATTR_ELEMENT_CORNER` attributes.)
- `vertex.tangent` — not read (needed for normal mapping correctness).
- `vertex.position` accepted with **no element-type validation** (Geometry.cpp:190-193,
  324-327, 474) — reject non-FLOAT32_VEC3 with a clear warning.
- Index arrays accept UINT32 only; spec also allows UINT64 variants.
- Sphere attributes stored as 3-component color, dropping the 4th channel
  (Geometry.cpp:519-561).
- `vertex.normal` only accepted as FLOAT32_VEC3; spec also allows FIXED16_VEC3.

## Notes accumulated from tasks 05-08 (2026-07-03)
- Spec params trimmed from introspection live in `device/json/cycles_khr_*.json` local
  registry copies — as you implement params, restore them there (curve/cylinder/cone copies
  trimmed primitive.color/attribute*/id and uniform attrs too; re-add for all subtypes you
  cover, including curve/cylinder/cone, not just triangle/quad/sphere).
- The vertex-attribute upload code is now TRIPLICATED (Triangle/Quad shared helpers, Curve
  lambda, Tube lambda) and has drifted: Curve/Tube added stale-attribute removal-on-unset
  and bounds clamps that Triangle/Quad lack (Triangle/Quad never drop stale attributes —
  pre-existing gap). Unify into one shared indexed-attribute helper (with an optional
  vertex-remap for Tube) as part of this task.
- Missing 'color' attribute renders BLACK (Cycles AttributeNode default) where the spec
  default is opaque white (1,1,1,1) — device-wide; affects CTS *_colors scores. Fix the
  default-when-absent behavior while unifying the helpers.
- CTS baseline after task 08: 11 passed / 150 failed / 153 skipped (.todo/cts-baseline.md
  has usage quirks). PSNR failures vs helide are expected device-wide; judge by coverage.

## Acceptance
`test_triangle_attributes` fully correct; CTS attribute tests pass; introspection matches
(closing the "advertised but ignored" gap from task 05).

## Suggested skills
`/tdd`; `/verify`; `/code-review`.
