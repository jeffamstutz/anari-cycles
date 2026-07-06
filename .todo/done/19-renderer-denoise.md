# Feature: declare KHR_RENDERER_DENOISE + make a denoiser available in the default build

**Type:** spec completeness / build hygiene
**Context:** `../IMPLEMENTATION_STATUS.md` §2.3, §4.1 · registry: `khr_renderer_denoise.json`

## Current state
- `denoise` renderer param fully implemented (device/Renderer.cpp:38-40, 109-137) including
  a workaround for Cycles' DENOISED→NOISY pass-mode downgrade — but gated on
  `WITH_OPTIX || WITH_OPENIMAGEDENOISE` at compile time, and the current build has neither
  (verified: "denoise requested but no denoiser compiled in" warnings in every render test).
- The KHR extension name is not declared; the param is only in the JSON as a plain entry.

## Goal
1. Declare `khr_renderer_denoise` in cycles_device.json (conditionally worth discussing —
   introspection is static, so either always declare and warn at runtime, or generate two
   JSON variants; simplest: declare, keep the runtime warning).
2. Get OIDN (CPU-friendly) enabled in the recommended build configuration; document the
   CMake flags in the README/BUILDING notes. OptiX denoiser when the OptiX backend is on.
3. Optional vendor params: denoiser choice, prefilter, start-sample
   (cycles/src/scene/integrator.h:123-130) — coordinate with task 24.

## Acceptance
1-sample render with `denoise=true` is visibly smoothed; extension shows in `anariInfo`.

## Suggested skills
`/verify`; `/code-review`.
