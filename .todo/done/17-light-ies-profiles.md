# Feature: intensityDistribution (IES-style photometric profiles) on quad/ring lights

**Type:** spec completeness (stretch) — differentiating
**Depends on:** 15-light-ring.md, 16-light-param-completion.md
**Context:** `../IMPLEMENTATION_STATUS.md` §4.1 · registry: `khr_light_quad.json`, `khr_light_ring.json`

## Spec surface
`intensityDistribution` (ARRAY1D or ARRAY2D FLOAT32/vec3): angular emission distribution;
`c0` (vec3) orients the C0 half-plane for 2D distributions (quad: also `edge1` interplay).

## Cycles mapping
Cycles has native IES support: `IESLightNode` (cycles/src/scene/shader_nodes.h:383) with
`ies` text content, plus `LightManager` IES slot management
(cycles/src/scene/light.h:210-212). The ANARI arrays are raw distributions, not IES files —
either synthesize an IES string from the array (straightforward text format) or drive the
light shader's strength by angle via math nodes (RampNode over incident angle).

## Acceptance
A cosine-shaped 1D distribution visibly narrows a quad light's beam vs the default;
symmetric distribution matches an analytically computed falloff at a few angles.

## Suggested skills
`/tdd`; `/verify`; `/code-review`.
