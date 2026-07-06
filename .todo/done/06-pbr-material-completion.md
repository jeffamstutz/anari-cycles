# Feature: complete KHR_MATERIAL_PHYSICALLY_BASED (+ matte alphaCutoff)

**Type:** spec completeness — high value, low risk
**Depends on:** 03-fix-attribute-color-plumbing.md (attribute/sampler bindings must work first)
**Context:** `../IMPLEMENTATION_STATUS.md` §2.2 · registry: `khr_material_physically_based.json`

## Current state
device/Material.cpp:135-169 reads baseColor, opacity, roughness, metallic, clearcoat,
clearcoatRoughness, emissive, transmission, ior, normal, alphaMode. Both materials map onto
a Cycles `PrincipledBsdfNode` (full socket list: cycles/src/scene/shader_nodes.h:548-580).

## Missing params → Cycles Principled sockets
- `specular` → `specular_ior_level`; `specularColor` → `specular_tint`
- `sheenColor`, `sheenRoughness` → `sheen_tint` / `sheen_roughness` (+ `sheen_weight`)
- `iridescence`, `iridescenceIor`, `iridescenceThickness` → thin-film sockets
  (`thin_film_thickness`, `thin_film_ior`)
- `clearcoatNormal` (sampler) → `coat_normal` input
- `occlusion` (sampler) — no direct Principled socket; multiply into base color or skip with
  a documented warning
- `thickness`, `attenuationColor`, `attenuationDistance` → transmission volume absorption
  (interior `AbsorptionVolumeNode`) — medium effort
- `alphaCutoff` (also add to matte, device/Material.cpp:39-48) for `alphaMode="mask"`
- Sampler support for `emissive`, `transmission`, `clearcoat`, `clearcoatRoughness`
  (currently constant/attribute only — Material.cpp:196-201)

## Acceptance
- `test_pbr_spheres` and CTS material tests improve; no regressions.
- Introspection declares exactly the params implemented (update cycles_device.json).

## Suggested skills
`/tdd`; `/verify` (render CTS material scenes side-by-side with helide); `/code-review`.
