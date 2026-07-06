# Feature: motion blur (instance motion transforms, camera motion, geometry deformation)

**Type:** spec completeness — flagship film-renderer feature
**Context:** `../IMPLEMENTATION_STATUS.md` §2.1, §4.1 · registry: `khr_instance_motion_transform.json`, `khr_instance_motion_scale_rotation_translation.json`, `khr_camera_motion_transformation.json`, `khr_geometry_*_motion_deformation.json`

## Spec surface
- Instance subtypes `motionTransform` (`motion.transform` mat4 array + `time` box1) and
  `motionScaleRotationTranslation` (`motion.scale`/`motion.rotation` quat/`motion.translation`
  arrays + `time`).
- Camera: `motion.transform`/`motion.scale`/`motion.rotation`/`motion.translation` + `time`.
- Geometry: triangle/quad `*_MOTION_DEFORMATION` — `vertex.position` as array-of-arrays +
  `time`.

## Cycles mapping (native support throughout)
- Object: `motion` (array<Transform>, up to `MAX_MOTION_STEPS=129`) +
  `use_motion_blur` (cycles/src/scene/object.h:52, 101).
- Camera: `motion` transform array, `use_perspective_motion` (camera.h:154-157).
- Geometry: `motion_steps` + `ATTR_STD_MOTION_VERTEX_POSITION` attribute
  (geometry.h:102-106).
- Enable `integrator->set_motion_blur(true)` and honor camera `shutter` (task 13).
- Note current instancing rebuilds `scene->objects` wholesale
  (device/World.cpp:73-97 `setCyclesWorldObjects`) — motion transforms slot in where
  `o->set_tfm(...)` happens (device/Group.cpp:43, Instance.cpp).
- `newInstance` currently ignores its subtype string (device/Device.cpp:139) — fix as part
  of adding subtypes.

## Acceptance
Moving instance renders streaked along its motion path; static scenes unchanged;
all four extensions declared and introspectable.

## Suggested skills
`/tdd`; `/verify`; `/code-review`.
