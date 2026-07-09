# Feature: KHR_SPATIAL_FIELD_NANOVDB (+ structuredRegularCubic filter)

> **UNBLOCKED + DONE (2026-07-08):** OpenVDB 11.0.1/NanoVDB 32.6 installed at
> `~/opt/.install/openvdb` and the build reconfigured with
> `WITH_CYCLES_NANOVDB=ON`/`WITH_CYCLES_OPENVDB=ON`. Implemented via Cycles'
> native VDB voxel-attribute path (see IMPLEMENTATION_STATUS.md §2.1); NanoVDB
> headers now also auto-fetch when missing (see BUILDING.md).

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

## How this was verified (2026-07-08)

Standalone behavioral test (throwaway, `/tmp/anari-nanovdb-test/test.cpp`; compile with
`g++ -O2 -std=c++17 test.cpp -I ~/opt/anari/include -I <nanovdb-include> -lanari`).
It renders a transferFunction1D volume (white color ramp, opacity ramp 0→1 so the
shader output tracks the sampled field value — a constant-opacity TF makes all filter
modes indistinguishable) at 128², 16 spp, camera on +Z, and asserts on mean luminance
/ mean-abs pixel diffs:

1. `nanovdb` field from an in-memory `nanovdb::createFogVolumeSphere<float>(30, 0, 1)`
   blob renders non-trivially (mean 0.163) — filter linear and nearest both.
2. The same grid densely resampled into an equivalent `structuredRegular` field
   (origin = indexBBox.min · voxelSize, spacing = voxelSize) matches the nanovdb
   render to <0.1% relative mean luminance (voxel-center alignment is exact).
3. `structuredRegular` 8³ random field: filter nearest / linear / cubic all render
   and differ pairwise (mean abs diffs 0.014 / 0.008 / 0.011), no error-severity
   status messages.

Regression: full `anariRenderTests` (13/13 render, gravity-spheres volume intact) and
whole-suite CTS at 128²/--accumulation 16: 17 passed / 158 failed / 139 skipped —
identical totals to the task 36 baseline (no CTS coverage exists for these extensions).
Build hygiene: clean reconfigure with only `OPENVDB_ROOT_DIR` set finds the NanoVDB
headers; the FetchContent fallback (headers absent) was exercised in isolation and
hash-verifies the OpenVDB v11.0.0 tarball.
