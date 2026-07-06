# Fix: volumes are never rendered (wire into scene + NanoVDB-backed voxel loader)

**Type:** P0 bugfix (verified)
**Context:** `../IMPLEMENTATION_STATUS.md` §1.5, §4.1 · setup in `README.md`

## Problem (two independent halves)
1. `Group::addGroupToCurrentCyclesScene()` (device/Group.cpp:26-62) creates Cycles objects
   for surfaces and lights only; the committed `volume` array is used for bounds but no
   `ccl::Object` is ever created. `Volume::makeCyclesGeometry()` (device/Volume.cpp:142) and
   `StructuredRegularField::makeCyclesGeometry()` (device/SpatialField.cpp:71) are dead code
   on the scene-build path.
2. `VolumeImageLoader::load_metadata/load_pixels` always return false
   (device/VolumeImageLoader.cpp:17-28) — Cycles removed dense 3D image textures, so voxels
   can't load even if the object existed.

Verified: `anariRenderTests demo_gravity_spheres_volume` renders a fully empty image.

## Goal
- Add volumes to the Cycles scene in `Group::addGroupToCurrentCyclesScene` (mirror the
  surface path; volume objects need transform + `set_geometry`).
- Replace the dead dense loader with a NanoVDB-backed one:
  `VDBImageLoader::grid_from_dense_voxels()` (cycles/src/scene/image_vdb.h:57) converts
  dense arrays to a grid Cycles can sample. Honor `origin`/`spacing` via grid transform.
- Map transferFunction1D `color`/`opacity` arrays onto the volume shader
  (ramp driven by field value; see device/Volume.cpp:81-140 for current params).
- Declare `khr_volume_transferFunction1D` + `khr_spatial_field_structuredRegular` in
  `device/cycles_device.json` once functional (coordinates with task 05).

## Acceptance
`demo_gravity_spheres_volume` renders a visible volume matching helide's output roughly;
`anariInfo` lists the volume + spatial field subtypes.

## Suggested skills
`/tdd` if adding tests; `/verify`; `/code-review`.
