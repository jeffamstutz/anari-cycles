# anari-cycles task queue

One file per task, produced from the 2026-07-03 gap analysis in `../IMPLEMENTATION_STATUS.md`.
Each task is a self-contained handoff for a fresh agent. Numbering is the suggested order;
tasks with no `Depends on` line can run in any order after the P0 bugfixes.

## Shared setup (referenced by every task)

```sh
cmake --build ~/build/claude/anari-cycles -j          # build (see memory: anari-cycles-build-recipe)
export ANARI_LIBRARY_PATH=~/build/claude/anari-cycles
export LD_LIBRARY_PATH=~/build/claude/anari-cycles:$HOME/opt/anari/lib
ANARI_LIBRARY=cycles ~/opt/anari/bin/anariInfo -l cycles
ANARI_LIBRARY=cycles ~/opt/anari/bin/anariRenderTests   # writes PNGs to cwd — run in a scratch dir
ANARI_LIBRARY=cycles ~/opt/anari/bin/anariCts            # conformance suite
```

- ANARI 1.1 extension/parameter registry (authoritative): `~/opt/anari/share/anari/code_gen/api/*.json`
- SDK source checkout: `~/src/ANARI-SDK`; build tree with viewer/tutorials: `~/build/ANARI-SDK`
- Device queries are code-generated from `device/cycles_device.json` (CMakeLists.txt:93-97) —
  any new subtype/param must be declared there to show up in introspection.
- Minimal behavioral-test pattern (render, assert average luminance):
  `/tmp/anari-cycles-rendertests/quadlight_test.c` (recreate from IMPLEMENTATION_STATUS.md §5.1 if gone).

## Index

**P0 — verified defects (do in order; 01 invalidates testing of everything else)**
- 01-fix-light-architecture.md
- 02-fix-point-light.md
- 03-fix-attribute-color-plumbing.md
- 04-fix-volume-rendering.md
- 05-introspection-json-sync.md

**Spec completeness (KHR)**
- 06-pbr-material-completion.md
- 07-geometry-curve.md
- 08-geometry-cone-cylinder.md
- 09-geometry-attribute-completion.md
- 10-sampler-completion.md
- 11-camera-depth-of-field.md
- 12-camera-omnidirectional.md
- 13-camera-shutter-stereo.md
- 14-motion-blur.md
- 15-light-ring.md
- 16-light-param-completion.md
- 17-light-ies-profiles.md
- 18-frame-completion.md
- 19-renderer-denoise.md
- 20-renderer-background-image.md
- 21-array1d-region.md
- 22-spatial-field-nanovdb.md
- 23-geometry-isosurface.md

**Vendor extensions (CYCLES_/EXT_)**
- 24-ext-renderer-sampling-controls.md
- 25-ext-visibility-holdout-shadow-catcher.md
- 26-ext-light-linking-lightgroups.md
- 27-ext-subdivision-surfaces.md
- 28-ext-point-cloud-spheres.md
- 29-ext-frame-channels.md
- 30-ext-osl-material.md
- 31-ext-material-sss-volume-toon.md
- 32-ext-device-selection.md
- 33-ext-sky-light.md

**Performance / interactivity**
- 35-interactive-resolution-scaling.md

**Build / deployment**
- 37-precompiled-gpu-kernels.md
