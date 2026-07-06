# Vendor ext: renderer sampling & integrator controls

**Type:** vendor extension (CYCLES_/EXT_) — simple Integrator/Session socket plumbing
**Context:** `../IMPLEMENTATION_STATUS.md` §4.2 · Cycles: `cycles/src/scene/integrator.h`, `session/session.h`

## Candidate renderer parameters (all direct socket sets)
- Bounces: `maxBounce`, `maxDiffuseBounce`, `maxGlossyBounce`, `maxTransmissionBounce`,
  `maxVolumeBounce`, `transparentMaxBounce` (integrator.h:43-56).
- **Adaptive sampling**: `use_adaptive_sampling`, `adaptive_threshold`,
  `adaptive_min_samples` (integrator.h:110-112) — currently *explicitly disabled*
  (device/Device.cpp:344, comment at :341-343 explains why; investigate the interaction
  with the 1-sample-per-frame accumulation model before enabling).
- Sample clamping: `sample_clamp_direct`, `sample_clamp_indirect` (integrator.h:93-94) —
  likely fixes the red fireflies seen in `test_pbr_spheres`.
- Light tree toggle + `light_sampling_threshold` (integrator.h:107-108).
- **Path guiding**: `use_guiding` + sub-params (integrator.h:67-78).
- Caustics: `caustics_reflective`, `caustics_refractive`, `filter_glossy`
  (integrator.h:80-82).
- Fast GI / AO approximation: `ao_bounces`, `ao_factor`, `ao_distance` (integrator.h:58-61).
- `time_limit`, `pixel_size`, `threads` (session.h:55-57).
- Pixel filter type/width + `exposure` (cycles/src/scene/film.h:29-44).

## Design notes
Pick a naming convention up front (suggest lowerCamel matching ANARI style, e.g.
`maxBounce`, `adaptiveSampling`, `clampIndirect`) and document each in cycles_device.json
so introspection carries descriptions. Renderer currently has one subtype "default"
(device/Device.cpp:159 ignores subtype) — consider adding a "fast"/"quality" preset subtype
later; keep this task to parameters only.

## Acceptance
Params visible in `anariInfo` with descriptions; a clamp value tames pbr_spheres fireflies;
bounce counts visibly change GI; no perf regression at defaults.

## Suggested skills
`/tdd`; `/verify`; `/code-review`.
