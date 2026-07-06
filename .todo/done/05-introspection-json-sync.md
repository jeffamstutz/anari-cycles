# Fix: sync cycles_device.json with the implementation; establish CTS baseline

**Type:** P0 hygiene (verified mismatch)
**Context:** `../IMPLEMENTATION_STATUS.md` §1.6, §3 · setup in `README.md`

## Problem
Device introspection is generated from `device/cycles_device.json` (CMakeLists.txt:93-97)
and is out of sync both directions:
- Implemented but undeclared: sampler `image2D` (only image1D declared); frame channels
  albedo/normal/objectId are wired to Cycles passes (device/Device.cpp:360-378,
  Frame.cpp:143-174) but not declared; accumulation + denoise behaviors exist without
  `khr_frame_accumulation` / `khr_renderer_denoise`.
- Declared/advertised but ignored: `anariInfo` lists `faceVarying.*`, `primitive.attribute*`,
  `primitive.id`, `vertex.tangent`, instance `color`/`attribute0..3`, camera `imageRegion` —
  none are read by the code.
- `anariInfo` shows EMPTY subtype lists for ANARI_SAMPLER / ANARI_SPATIAL_FIELD / ANARI_VOLUME.

## Goal
Make the JSON tell the truth (add what exists, trim what's ignored — or note as TODO for
the tasks that implement them), regenerate, and verify with `anariInfo`. Then run
`anariCts` and record a baseline results file (suggest `.todo/cts-baseline.md` or a docs/
location) so later tasks can show conformance deltas.

## Notes
- SDK tooling: `~/opt/anari/share/anari/code_gen/validate_device.py` diffs a device JSON
  against the spec registry; `merge_anari.py`/`generate_queries.py` are the generators.
- Registry JSONs list exact param names/types per extension — copy from there, don't guess.

## Acceptance
`anariInfo -l cycles` subtype/param output matches what the code actually reads;
CTS baseline recorded; no regressions in `anariRenderTests`.

## Suggested skills
`/verify`; `/code-review`.
