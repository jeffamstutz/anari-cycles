# Vendor ext: Catmull-Clark subdivision surfaces (+ adaptive dicing)

**Type:** vendor extension — flagship film-renderer feature
**Context:** `../IMPLEMENTATION_STATUS.md` §4.2 · Cycles: `cycles/src/scene/mesh.h:117-174`

## Cycles capability
- `SubdivisionType`: NONE / LINEAR / CATMULL_CLARK (mesh.h:117); boundary + face-varying
  interpolation modes (mesh.h:123-129).
- Adaptive dicing: `subd_adaptive_space` (PIXEL/OBJECT), `subd_dicing_rate`,
  `subd_max_level` (mesh.h:138, 169-171); edge/vertex creases (mesh.h:154-166);
  separate `subd_attributes` set.

## Proposed ANARI surface (params on triangle/quad geometry)
- `subdivision` (STRING: none/linear/catmullClark), `subdivisionLevel` (max level),
  `subdivisionDicingRate` (FLOAT32, pixels), plus optional
  `primitive.creaseIndex`/`primitive.creaseWeight` arrays for creases.
- Quad geometry is the natural host (Catmull-Clark wants quads); triangles work too.

## Notes
Requires feeding Cycles subd face arrays instead of plain triangles (`subd_faces` path) —
this is a different mesh-build path than the current one in device/Geometry.cpp.
Adaptive dicing interacts with camera (dicing camera) — start with object-space dicing.

## Acceptance
Low-poly quad cube with catmullClark renders as smooth sphere-ish surface; dicing rate
visibly trades detail vs memory; off by default with zero overhead.

## Suggested skills
`/tdd`; `/verify`; `/code-review`.
