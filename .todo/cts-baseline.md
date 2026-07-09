# CTS baseline — 2026-07-03

Recorded after the introspection sync task (05). SDK 0.16.0, `~/opt/anari/bin/anariCts`,
device built from this repo at the commit that adds this file.

## Commands used

```sh
export ANARI_LIBRARY_PATH=~/build/claude/anari-cycles
export LD_LIBRARY_PATH=~/build/claude/anari-cycles:$HOME/opt/anari/lib

# 1) ground truth with the reference device (128x128 to keep runtime sane)
anariCts generate --device helide --workdir wd --width 128 --height 128
#   -> generate (helide): 191 generated, 123 skipped, 0 failed (of 314)

# 2) candidate run — full-suite runs crash (see below), so each of the 83 tests
#    was run in its own process; '?' forces exact glob matching of the filter:
while read t; do
  anariCts run cycles --workdir wd --filter "?${t#?}" \
    --no-accumulation --width 128 --height 128
done < tests.txt   # tests.txt = "<category>/<test>" lines from `anariCts list`
```

Gotchas discovered (so the next run doesn't rediscover them):
- **Render size must match the ground truth size.** Mismatched sizes silently produce
  `psnr: null` metrics and every case "fails". Pass the same `--width/--height` to
  `generate` and `run`.
- `--filter` is substring-or-glob; globs are full-match. `geometry/triangle` substring-
  matches all `triangle*` tests; `?eometry/triangle` selects exactly one test.
- `--no-accumulation` is required for stability (below).

## Results (per-test isolated runs, 128x128, --no-accumulation)

Totals: **83 test groups — 5 crashed (SIGSEGV)**; of the 296 cases in the 78 surviving
groups: **7 passed, 98 failed, 191 skipped**. (314 cases total; the 18 cases in crashed
groups were not scored.)

- Crashed groups this run: `geometry/triangle`, `geometry/triangle_quad`,
  `geometry/triangle_cube`, `geometry/quad_cube`, `volume/volume`.
  Crashes are **nondeterministic** — a 256x256 pass of the same loop crashed a
  different set (`geometry/triangle_normal_tangent`, `volume/volume`). This is the
  pre-existing render-session race tracked in `.todo/34-fix-render-session-race.md`
  (gdb backtrace: Cycles render thread in
  `ObjectManager::device_update_transforms` → `AttributeSet::find` on a dangling
  geometry while the API thread mutates the scene). Not introduced by task 05:
  HEAD~0 (before this task's changes) runs the same filters without crashing only
  because it renders fewer frames per test; with `--accumulation 16` (the CTS default
  once the device claims `KHR_FRAME_ACCUMULATION`) whole-suite runs crash within
  seconds, `--accumulation 4` usually survives, `--no-accumulation` is stable per-test
  but whole-suite runs (~300 renders in one process) still crash eventually.
- Passed cases (7): `instance/instance:default`, `frame/progressive_rendering`,
  3 of 4 `renderer/renderer_background_color`, 1 of 4 `camera/camera_general`,
  1 of 4 `geometry/triangle_normal_tangent` (`unset_unset`).
- Skipped cases (191): tests gated on extensions the device doesn't claim
  (cone/cylinder/curve/isosurface, image3D/primitive/transform samplers, ring light,
  omnidirectional camera, primitiveId/instanceId channels, completion callback,
  background image) **plus** cases helide produced no ground truth for during
  `generate` (123 of 314) — notably all `light/*` and most `material/pbr_*` cases skip
  for lack of ground truth even though cycles could render them.
  `anariCts check-properties cycles`: 55 of 83 test groups runnable, 28 skipped.
- Failed cases (98): scoring failures against helide ground truth, not errors. Typical
  color-channel PSNR is 10–19 vs the 20.0 threshold (SSIM 0.4–0.9 vs 0.7) — expected
  for a 1-sample path tracer diffed against helide's analytic renderer (soft shadows,
  GI, different background/ambient response, noise). Worst offenders are
  vertex/primitive color tests (PSNR ~5) and depth channels for spheres (PSNR 1.5),
  which look like real gaps worth investigating in later tasks, not just noise.

Raw per-test tallies from this run are reproducible with the loop above; the scored
JSON reports land in `wd/results/<category>/<test>/*.json` (PSNR/SSIM per channel).

## Update after task 34 (render-session race fix, 2026-07-03)

Whole-suite runs are now stable — no per-test process isolation needed:

```sh
anariCts run cycles --workdir wd --width 128 --height 128 --accumulation 16
#   -> run (cycles): 10 passed, 113 failed, 191 skipped (of 314)
```

Deterministic across 5+ repeats (was: SIGSEGV within seconds at accumulation 16).
Counts differ from the old per-test numbers because all 314 cases are now scored in
one process (the 5 previously-crashing groups score instead of dying): 10 passed vs 7,
113 failed vs 98 (+18 formerly unscored, -3 that pass under accumulation).

## Baseline interpretation for later tasks

Treat as the conformance floor: 7/117 scored channel-cases pass today. Deltas to watch:
- Any newly claimed extension flips its tests from "skipped" to scored.
- Fixing the session race (task 34) should make whole-suite and accumulation runs
  usable (and is a prerequisite for trusting any CTS numbers at defaults).
- Regenerating ground truth with a better reference (e.g. `visrtx` or `ospray` instead
  of `helide`) would make the PSNR comparison meaningful for a path tracer; consider
  `--device ospray` next time.

## Update after task 09 (geometry attribute completion, 2026-07-04)

Same command as task 34's update (whole suite, 128x128, --accumulation 16):
totals unchanged at **11 passed / 150 failed / 153 skipped**, but the scored
attribute groups improved markedly:
- `geometry/triangle_normal_tangent`: **4/4 pass** (was 1/4 — explicit
  vertex.normal/tangent arrays are now honored; tangents feed Cycles'
  ATTR_STD_UV_TANGENT for normal mapping).
- `geometry/*_colors` primitive.color cases now render (were black):
  triangle 17.5 / quad 15.5 / cylinder 18.3 / cone 11.0 / curve 20.0 PSNR —
  still below the 20.0 pass threshold vs helide (expected device-wide gap).
- `geometry/sphere_colors` still scores PSNR 4.8 on all four cases: colors are
  visually correct but the candidate spheres render ~2x too large vs helide
  ground truth — a sphere *radius* bug, not an attribute bug (matches the
  "depth channels for spheres PSNR 1.5" note above). Worth its own task.

## Update after task 10 (sampler completion, 2026-07-04)

Same command (whole suite, 128x128, --accumulation 16):
**12 passed / 161 failed / 141 skipped** (was 11/150/153). The 12 newly scored
cases are `sampler/primitive` (8) and `sampler/transform` (4), which flipped
from skipped to scored when the extensions were claimed; all land in the
13-18 PSNR device-wide gap band (helide analytic shading vs cycles path
tracing), plus one newly passing image1D case (inOffset).

Ground-truth caveats discovered (helide bugs make some sampler cases
unwinnable):
- helide's `TransformSampler` reads legacy `"transform"` (not registry
  `outTransform`) and ignores `outOffset`; the CTS also sets `"transform"`,
  so this device accepts `transform` as an alias. The
  `transform:<matrix>_<outOffset>` case scores 11.3 because we honor
  `outOffset` and the ground truth does not.
- helide's `PrimitiveSampler` reads `"offset"` with a type mismatch (CTS
  sends INT32) so its ground truth ignores the offset entirely; this device
  implements registry `inOffset` (UINT64). CTS `offset=1` cases match GT only
  because both end up at offset 0.
- CTS `sampler/primitive` runs exposed a real bug (fixed): Cycles'
  ImageManager dedupes new images against every live slot via
  `ImageLoader::equals()`, which compared raw ANARI array *pointers*; a freed
  array reallocated at the same address deduped onto a stale image. The
  loader now pins the array (IntrusivePtr) and compares element types too.

## Update after task 36 (geometry motion deformation, 2026-07-08)

Fresh helide ground truth (190 generated, 124 skipped — one fewer than the
2026-07-03 run) and the whole-suite command (128x128, --accumulation 16):
**17 passed / 158 failed / 139 skipped**, byte-for-byte identical per-case
verdicts between this task's build and its parent commit (only float metric
noise differs). The CTS has no KHR_GEOMETRY_*_MOTION_DEFORMATION tests, so
claiming the extensions flips nothing from skipped to scored. anariRenderTests
is also identical to the parent commit (12/13 PNGs byte-equal;
perf_spinning_cubes differs run-to-run even on one build — animated test).

## Update after task 22 (nanovdb spatial field + cubic filter, 2026-07-08)

Fresh helide ground truth (190 generated, 124 skipped) and the whole-suite
command (128x128, --accumulation 16): **17 passed / 158 failed / 139
skipped** — totals identical to the task 36 run. The CTS has no
KHR_SPATIAL_FIELD_NANOVDB / STRUCTURED_REGULAR_CUBIC tests, so claiming them
flips nothing from skipped to scored, and the default (linear) structuredRegular
path is untouched (volume/volume verdicts unchanged). Primary verification was
a standalone behavioral test (see the task file): a NanoVDB fog-volume sphere
renders and matches a densely-resampled structuredRegular equivalent to <0.1%
mean luminance, filter=nearest/linear differ on the nanovdb path, and
nearest/linear/cubic all differ pairwise on an 8^3 structuredRegular field.
