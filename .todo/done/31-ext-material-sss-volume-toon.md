# Vendor ext: subsurface scattering, principled volume material, toon/hair BSDFs

**Type:** vendor extension — material breadth
**Depends on:** 06-pbr-material-completion.md
**Context:** `../IMPLEMENTATION_STATUS.md` §4.2 · Cycles: `cycles/src/scene/shader_nodes.h`

## Candidates (separate PRs, one design)
1. **SSS on physicallyBased** (spec PBR has none): vendor params `subsurface`,
   `subsurfaceColor`/`subsurfaceRadius`, `subsurfaceScale`, `subsurfaceAnisotropy` →
   Principled `subsurface_*` sockets (shader_nodes.h:548-580). Cycles random-walk SSS is
   excellent — high visual payoff for skin/wax/marble.
2. **Principled volume material** (`CYCLES_MATERIAL_PRINCIPLED_VOLUME`): density, color,
   anisotropy, absorption color, emission strength/color, blackbody
   (PrincipledVolumeNode, shader_nodes.h:914) — richer than transferFunction1D for
   smoke/fire; attach to ANARI Volume or as a material on volumes (needs design: ANARI
   volumes are not surfaces — likely new Volume subtype instead of Material).
3. **Toon** (ToonBsdfNode :728) and **hair** (PrincipledHairBsdfNode :937) material
   subtypes — hair pairs with task 07 (curve geometry).
4. `emissiveStrength`-style param beyond PBR `emissive` color (Principled emission
   strength socket).

## Acceptance
Each material renders a canonical test object; introspection documents all params;
no interference with KHR materials.

## Suggested skills
`/design-an-interface` for the volume-material question; `/tdd`; `/verify`; `/code-review`.
