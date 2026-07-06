# Feature: sampler completion (transform + primitive subtypes; image1D/2D gaps)

**Type:** spec completeness
**Context:** `../IMPLEMENTATION_STATUS.md` §2.1, §2.2 · registry: `khr_sampler_*.json`

## Gaps
Missing subtypes (verified warnings at runtime):
- **`transform`** — pure graph math: output = `outTransform * inAttribute + outOffset`.
  Easiest; no image involved. (khr_sampler_transform.json)
- **`primitive`** — per-primitive array lookup by primitiveId + `inOffset` (UINT64).
  Map to a Cycles per-primitive attribute created from the array. (khr_sampler_primitive.json)
- **`image3D`** — blocked: Cycles dropped dense 3D textures (device/Sampler.cpp:408-410).
  Feasible via NanoVDB conversion (see task 04/22 infrastructure); decide whether worth it.

Defects in existing image1D/image2D (device/Sampler.cpp):
- `inAttribute` read but **ignored** — graph always samples attribute0
  (Sampler.cpp:60-74, Material.cpp:354). Honor it (color, attribute0..3, primitiveId...).
- `mirrorRepeat` silently maps to extend (Sampler.cpp:32-40); Cycles has
  `EXTENSION_MIRROR`.
- `inTransform` reduced to scale+translation; rotation/shear dropped (Sampler.cpp:211-228).
  Cycles `MappingNode` can do the full affine.
- image1D doesn't read `inAttribute`/`wrapMode1` (Sampler.cpp:130-138).
- UFIXED32 pixel formats rejected (SamplerImageLoader.cpp:13-47) — seen in render tests.
- `KHR_SAMPLER_IMAGExD_CLAMP_TO_BORDER` (borderColor) — optional stretch.
- **No alpha output**: `Sampler::SamplerOutputs` (Sampler.h) exposes only
  color/scalar/normal; texture alpha is unreachable. Spec (verified against helide,
  Renderer.cpp:288) multiplies the `color`/`baseColor` source's 4th component into
  opacity before alphaMode/alphaCutoff — RGBA cutout textures on `baseColor` therefore
  render fully opaque today (found during task 06 review, 2026-07-03). Add an
  alphaOutput and wire it into `Material::connectAlpha`.

## Acceptance
CTS sampler tests pass for image1D/2D/transform/primitive; `test_textured_cube` unchanged;
cycles_device.json declares exactly what works (incl. the already-implemented image2D).

## Suggested skills
`/tdd`; `/verify`; `/code-review`.
