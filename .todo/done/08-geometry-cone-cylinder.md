# Feature: KHR_GEOMETRY_CYLINDER + KHR_GEOMETRY_CONE

**Type:** spec completeness
**Context:** `../IMPLEMENTATION_STATUS.md` §2.1 · registry: `khr_geometry_cylinder.json`, `khr_geometry_cone.json`

## Spec surface
- `cylinder`: `vertex.position` [req], `primitive.index` (UINT32_VEC2), `primitive.radius`,
  global `radius`, `vertex.cap` (UINT8), `caps` string (none/first/second/both), colors/attrs.
- `cone`: `vertex.position` [req], `vertex.radius` (per-vertex!), `primitive.index`
  (UINT32_VEC2), `vertex.cap`, `caps`, colors/attrs.

Currently both warn "unknown ANARI_GEOMETRY subtype" (verified in anariRenderTests:
`test_random_cylinders` renders without the cylinders).

## Cycles mapping options
Cycles has no analytic cylinder/cone primitive. Options:
1. **Mesh tessellation** (recommended first pass): generate a triangle mesh per primitive
   (N-gon cross-section, configurable), reusing the triangle path's attribute plumbing.
2. Hair with `CURVE_THICK_LINEAR` approximates capless cylinders/cones (per-key radius gives
   cones) — cheap but caps and exact silhouettes are wrong.

## Acceptance
`test_random_cylinders` renders cylinders; CTS cone/cylinder tests pass visually;
extensions declared in cycles_device.json.

## Suggested skills
`/tdd`; `/verify`; `/code-review`.
