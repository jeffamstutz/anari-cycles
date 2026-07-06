# Fix: sphere (PointCloud) rendering defects — size, invisibility, absorption

**Type:** bugfix cluster (observed 2026-07-03..05 during tasks 09/06/21)
**Context:** spheres are implemented as Cycles `ccl::PointCloud` (device/Geometry.cpp
Sphere subtype). Three independent defects accumulated:

## 1. Spheres render ~2x too large vs helide (task 09)
CTS sphere cases (e.g. `geometry/sphere_colors`, PSNR ~4.8; depth PSNR ~1.5) show spheres
roughly twice expected size. Suspect radius-vs-diameter mismatch in the PointCloud radius
upload, or a Cycles point-primitive convention difference. Verify ANALYTICALLY (a radius-r
sphere at distance d with fovy f covers a predictable pixel extent) — don't just match
helide blindly.

## 2. Spheres with no color attribute render invisible (task 21)
Cycles PointCloud shading can't read the `ATTR_ELEMENT_MESH` `DEFAULT_COLOR` constant
attribute the device uploads for missing color (added in task 09) → color evaluates with
alpha 0 → invisible. Reproduce: sphere geometry with only vertex.position, matte material,
no color anywhere. Fix so missing color yields opaque white like the mesh paths (maybe
upload the constant as a per-point attribute for PointCloud, or bake the default into the
material graph when geometry is a point cloud).

## 3. Interior absorption shows only rim artifacts on spheres (task 06)
PBR `thickness`/`attenuationColor`/`attenuationDistance` (interior AbsorptionVolumeNode)
works on mesh geometry but Cycles point-cloud spheres show only rim artifacts — likely
points aren't treated as closed volumes by the volume stack. Investigate; may be a
documented limitation (points may need mesh fallback when interior volume is requested).

## Acceptance
Analytic silhouette test passes (item 1); attribute-less sphere renders opaque white
(item 2); CTS sphere group PSNR improves markedly; no regression in test_random_spheres /
perf_particles; item 3 fixed or documented as a limitation with a runtime warning.

## Suggested skills
`/diagnosing-bugs`; `/verify`; `/code-review` (synchronous only).
