# Feature: KHR_CAMERA_OMNIDIRECTIONAL

**Type:** spec completeness — trivial Cycles mapping
**Context:** `../IMPLEMENTATION_STATUS.md` §2.1, §4.1 · registry: `khr_camera_omnidirectional.json`

## Spec surface
New camera subtype `omnidirectional`: `position`, `direction`, `up`, `imageRegion`,
`layout` (STRING, only defined value `"equirectangular"`).

## Cycles mapping
`camera->set_camera_type(CAMERA_PANORAMA)` +
`camera->set_panorama_type(PANORAMA_EQUIRECTANGULAR)`
(cycles/src/scene/camera.h:81-102, kernel/types.h:487-491).
Add subtype in `Camera::createInstance` (device/Camera.cpp:44-49); reuse the base
position/direction/up look-at plumbing (Camera.cpp:74-97) — verify Cycles' panorama
convention against ANARI's (direction = center of the equirect image).

Cycles also has fisheye equidistant/equisolid, mirror ball, central cylindrical, and
fisheye lens polynomial — good candidates for a `CYCLES_CAMERA_PANORAMIC` vendor extension
as a follow-up in the same file.

## Acceptance
360° render of a test scene wraps correctly (left edge = right edge, poles at top/bottom);
extension declared in cycles_device.json.

## Suggested skills
`/verify`; `/code-review`.
