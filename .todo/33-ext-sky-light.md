# Vendor ext: procedural sun+sky light (Cycles SkyTexture)

**Type:** vendor extension — nice demo feature
**Depends on:** 01-fix-light-architecture.md
**Context:** `../IMPLEMENTATION_STATUS.md` §4.2 · Cycles: `cycles/src/scene/shader_nodes.h:177` (SkyTextureNode)

## Idea
A `CYCLES_LIGHT_SKY` (or `sky`) light subtype: physically-based sun+sky dome
(Nishita model in current Cycles) without users providing an HDRI.
Params: `sunDirection` (vec3), `turbidity`/`altitude`/`airDensity`/`dustDensity`/
`ozoneDensity` (match SkyTextureNode sockets — read the node header for the exact set),
`intensity`/`scale`, `sunDisc` (bool), `sunSize`.

## Cycles mapping
Follow the HDRI light pattern exactly (device/Light.cpp:250-330): a `BackgroundLight` whose
shader graph is `SkyTextureNode → BackgroundNode` instead of an image texture. Interacts
with `World::setupHDRIBackground` (device/World.cpp:149-160) — first background-type light
wins; sky and hdri should share that slot cleanly (document conflict behavior).

## Acceptance
Sky light alone produces plausible daylight with visible sun disc; changing sun elevation
shifts color temperature; coexists correctly with (or cleanly conflicts with) an HDRI light.

## Suggested skills
`/tdd`; `/verify`; `/code-review`.
