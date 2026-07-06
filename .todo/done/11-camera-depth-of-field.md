# Feature: KHR_CAMERA_DEPTH_OF_FIELD

**Type:** spec completeness — trivial Cycles mapping
**Context:** `../IMPLEMENTATION_STATUS.md` §2.2, §4.1 · registry: `khr_camera_depth_of_field.json`

## Spec surface
Two params on any camera: `apertureRadius` (FLOAT32, default 0 = pinhole) and
`focusDistance` (FLOAT32, default 1).

## Cycles mapping
Direct: `camera->set_aperturesize(apertureRadius)` and
`camera->set_focaldistance(focusDistance)` (cycles/src/scene/camera.h:75-78).
Wire into device/Camera.cpp base `commitParameters` (Camera.cpp:54-56) so both
perspective and orthographic get it. Cycles also offers `blades`/`bladesrotation` —
consider a CYCLES_ vendor param pair while in here.

## Acceptance
Render with nonzero apertureRadius shows focal blur; default stays pinhole-sharp;
`khr_camera_depth_of_field` declared in cycles_device.json.

## Suggested skills
`/verify` (visual check is the whole point); `/code-review`.
