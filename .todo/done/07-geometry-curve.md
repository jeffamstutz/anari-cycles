# Feature: KHR_GEOMETRY_CURVE via Cycles Hair

**Type:** spec completeness
**Context:** `../IMPLEMENTATION_STATUS.md` §2.1, §4.1 · registry: `khr_geometry_curve.json`

## Spec surface
`curve` subtype: `vertex.position` [req], `vertex.radius` (float array), `vertex.color`,
`vertex.attribute0..3`, `primitive.index` (UINT32/UINT64 — index into vertices marking
segment starts), global `radius`.

## Cycles mapping
`Hair` geometry (cycles/src/scene/hair.h:13): `curve_keys` (float3), per-key `curve_radius`,
`curve_first_key`, per-curve `curve_shader`. Choose `curve_shape`:
`CURVE_THICK_LINEAR` matches ANARI's round linear-segment curve semantics best
(kernel/types.h:668). ANARI segments (index = first vertex of each 2-vertex segment) need
conversion to Cycles curves-with-keys — consecutive segments sharing vertices can merge
into one curve; otherwise emit 2-key curves.

## Pattern to follow
Existing subtypes in device/Geometry.cpp (sphere is closest — per-vertex radius plumbing at
Geometry.cpp:454-481). Register in `Geometry::createInstance` (Geometry.cpp:643-650),
declare `khr_geometry_curve` in cycles_device.json.

## Acceptance
`anariRenderTests` scenes using curve (and CTS curve tests) render; attributes/color work.

## Suggested skills
`/tdd`; `/verify`; `/code-review`.
