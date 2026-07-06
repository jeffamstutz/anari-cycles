# Fix: point light emits nothing; add light-type behavioral test matrix

**Type:** P0 bugfix (verified)
**Depends on:** 01-fix-light-architecture.md
**Context:** `../IMPLEMENTATION_STATUS.md` §1.3 · setup in `README.md`

## Problem
Verified: a point light (`intensity=10`, position above a matte floor) contributes zero
illumination even when the shared-shader issue from task 01 is worked around
(ambientRadiance=0.25 enables the shader; an equivalent quad light lights the floor,
the point light does not).

## Where to look
- device/Light.cpp:379-407 (`Point::commitParameters/finalize`) — strength units,
  radius=0 handling, transform placement via `Point::xfm()`.
- Cycles side: `PointLight` in cycles/src/scene/light.h:89 (`radius`, `is_sphere`);
  `Light::copy_to_kernel` in cycles/src/scene/light.cpp.
- Check whether Cycles disables zero-radius point lights or mis-scales strength
  (Cycles point light strength is total watts vs ANARI `intensity` = W/sr).

## Goal
Point light illuminates correctly with ANARI photometric semantics
(`intensity` W/sr; `power` W once task 16 adds it). While here, run the same behavioral
test for spot and directional to confirm they work (spot uses the same position/direction
plumbing; directional verified working only indirectly so far).

## Acceptance
Behavioral test matrix: for each of {point, spot, directional, quad}, render floor +
single light with ambient=0 and assert average luminance > threshold; all pass.

## Suggested skills
`/diagnosing-bugs` (this one has an unknown root cause); `/tdd` for the test matrix;
`/verify`, `/code-review`.
