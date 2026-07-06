# Feature: KHR_CAMERA_SHUTTER (+ rolling shutter) and KHR_CAMERA_STEREO

**Type:** spec completeness
**Depends on:** 14-motion-blur.md is the consumer of shutter — coordinate
**Context:** `../IMPLEMENTATION_STATUS.md` §2.2, §4.1 · registry: `khr_camera_shutter.json`, `khr_camera_rolling_shutter.json`, `khr_camera_stereo.json`

## Spec surface
- Shutter: `shutter` (FLOAT32_BOX1, default [0.5, 0.5]) on any camera — the time interval
  the shutter is open (meaningful once motion blur exists).
- Rolling shutter (extends shutter): `rollingShutterDirection` (none/left/right/down/up),
  `rollingShutterDuration`.
- Stereo: `stereoMode` (none/left/right/sideBySide/topBottom), `interpupillaryDistance`
  (default 0.0635).

## Cycles mapping
- `shuttertime`, `motion_position`, `shutter_curve` (cycles/src/scene/camera.h:61-63).
- `rolling_shutter_type` + `rolling_shutter_duration` (camera.h:44-72) — Cycles only has
  RollingShutterType TOP; map directions accordingly or warn.
- Stereo: `stereo_eye`, `interocular_distance`, `convergence_distance`, plus spherical
  stereo for panoramas (camera.h:54-111). sideBySide/topBottom need two renders or a
  per-eye camera — decide semantics (per-eye subframe like other devices do).

## Acceptance
Shutter interval visibly changes blur length once task 14 lands; stereo left/right renders
differ by IPD; extensions declared.

## Suggested skills
`/tdd`; `/verify`; `/code-review`.
