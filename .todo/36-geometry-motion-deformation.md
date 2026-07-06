# Feature: KHR_GEOMETRY_TRIANGLE/QUAD_MOTION_DEFORMATION

**Type:** spec completeness (deferred from task 14, 2026-07-04)
**Depends on:** done/14-motion-blur.md machinery (device/MotionTrack.h, shutter contract)
**Context:** registry: `khr_geometry_triangle_motion_deformation.json`, `khr_geometry_quad_motion_deformation.json`

## What's needed (from task 14's analysis)
- `vertex.position` (and optional `vertex.normal`/`vertex.tangent`) as ARRAY1D-of-ARRAY1D
  (nested object arrays — check how helium exposes an ObjectArray of Array1D handles) +
  `time` FLOAT32_BOX1 → Cycles `ATTR_STD_MOTION_VERTEX_POSITION` with geometry
  `motion_steps`.
- The hard part: geometry sync happens in `Surface::finalize`, decoupled from the
  frame/shutter — shutter-dependent vertex re-baking needs new re-sync plumbing (task 14
  added per-world shutter re-bake for instance/camera motion in `World::motionRequiresRebake`;
  geometry needs an analogous hook).
- Cycles motion vertex attribute layout: odd step counts with the center step stored in
  the base mesh (see how Cycles' own importers lay out `ATTR_STD_MOTION_VERTEX_POSITION` —
  the base `verts` is the center step, motion attribute holds the other N-1 steps).
- Bake per the task-13 shutter contract: step i of N at `s0 + (s1−s0)·i/(N−1)`,
  resampling the ANARI `time` box with linear interpolation.

## Acceptance
Deforming triangle mesh (e.g. vertex moving across the frame) renders a streak matching
analytic extent under shutter [0,1]; degenerate shutter renders sharp midpoint pose;
extensions declared with truthful JSON copies; anariRenderTests regression-clean.

## Suggested skills
`/tdd`; `/verify`; `/code-review` (synchronous only).
