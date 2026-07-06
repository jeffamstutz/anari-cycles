# Feature: KHR_SPATIAL_FIELD_NANOVDB (+ structuredRegularCubic filter)

> **BLOCKED on this machine (2026-07-03, noted during task 04):** the current build has
> `WITH_CYCLES_NANOVDB=OFF`/`WITH_CYCLES_OPENVDB=OFF` and no VDB headers are installed.
> Task 04 shipped volumes via a 2D-atlas sampling workaround instead
> (`SpatialField::createCyclesSamplingNodes`); a NanoVDB field can slot in as another
> implementation of that seam once NanoVDB headers are installed and the kernel is rebuilt
> with `WITH_CYCLES_NANOVDB=ON`. Requires user action first — do not attempt until unblocked.

**Type:** spec completeness — *direct* Cycles fit, high value for volume users
**Depends on:** 04-fix-volume-rendering.md · **user installing NanoVDB + reconfigure**
**Context:** `../IMPLEMENTATION_STATUS.md` §2.1, §4.1 · registry: `khr_spatial_field_nanovdb.json`

## Spec surface
`nanovdb` spatial field subtype: `data` [req] (ARRAY1D UINT8 — a serialized NanoVDB grid),
`filter` (nearest/linear, default linear).

## Cycles mapping
Cycles consumes VDB natively: `VDBImageLoader` (cycles/src/scene/image_vdb.h:26) with
NanoVDB support under `WITH_NANOVDB` (precision 16 default, image_vdb.h:71-72). The task is
mostly constructing/wrapping the grid from the ANARI byte blob and registering it as a
volume attribute image, then reusing the transferFunction1D shader path from task 04.
Verify `WITH_NANOVDB`/`WITH_OPENVDB` are on in the build (check CMakeCache in
~/build/claude/anari-cycles); if not, enable and note flags in BUILDING docs.

Also in scope: `KHR_SPATIAL_FIELD_STRUCTURED_REGULAR_CUBIC` — Cycles interpolation
`VOLUME_INTERPOLATION_CUBIC` via shader `volume_interpolation_method`
(cycles/src/scene/shader.h:50) makes `filter="cubic"` nearly free; and the structuredRegular
`filter` param (nearest/linear) which the audit found missing.

## Acceptance
A NanoVDB test asset renders; structuredRegular `filter` honored; extensions declared.

## Suggested skills
`/tdd`; `/verify`; `/code-review`.
