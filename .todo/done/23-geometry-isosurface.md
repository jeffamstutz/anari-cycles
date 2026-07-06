# Feature: KHR_GEOMETRY_ISOSURFACE

**Type:** spec completeness — hard; do after volume infrastructure
**Depends on:** 04-fix-volume-rendering.md, 22-spatial-field-nanovdb.md
**Context:** `../IMPLEMENTATION_STATUS.md` §2.1 · registry: `khr_geometry_isosurface.json`

## Spec surface
`isosurface` geometry subtype: `isovalue` [req] (FLOAT32 or array — multiple surfaces),
`field` [req] (ANARI_SPATIAL_FIELD), plus uniform/primitive color+attribute params.

## Cycles mapping options
Cycles does not ray-trace implicit isosurfaces. Options:
1. **Mesh extraction** (recommended): marching cubes over the field at commit time →
   triangle Mesh. OpenVDB has `tools::volumeToMesh` and Cycles links OpenVDB already —
   check availability in the vendored build. Re-extract on isovalue/field change.
2. Volume trick with a sharp transfer function — wrong silhouettes/normals; don't.

Watch memory/time for large fields; consider vendor param for extraction resolution.

## Acceptance
Isosurface of the gravity-spheres field renders with correct normals and material;
multiple isovalues produce nested shells; extension declared.

## Suggested skills
`/tdd`; `/verify`; `/code-review`.
