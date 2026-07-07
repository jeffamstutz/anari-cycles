# Vendor ext: additional frame channels (cryptomatte, mist, motion vectors, AOVs, ...)

**Type:** vendor extension — DCC/compositing integration
**Depends on:** 18-frame-completion.md (channel plumbing patterns)
**Context:** `../IMPLEMENTATION_STATUS.md` §4.2 · Cycles: `cycles/src/scene/pass.h`, kernel/types.h:299-388

## Cycles passes worth exposing as `channel.*`
Already have combined/depth/normal/diffuse_color/object_id (device/Device.cpp:360-378).
Candidates, roughly by value:
- `channel.position` (PASS_POSITION), `channel.mist` (PASS_MIST + Film mist params),
  `channel.motion` (PASS_MOTION, needs motion blur from task 14), `channel.roughness`.
- **Cryptomatte** (PASS_CRYPTOMATTE + `Film::cryptomatte_passes/depth`, film.h:46-47;
  object/asset name maps in ObjectManager, object.h:189-190) — multi-layer output; needs a
  channel-naming scheme (`channel.cryptomatte00`...).
- Shadow catcher: PASS_SHADOW_CATCHER / _MATTE (pairs with task 25).
- Light AOVs: PASS_AOV_COLOR / PASS_AOV_VALUE via `OutputAOVNode` in shaders — likely a
  later follow-up once custom shader graphs exist.
- `channel.sampleCount` (PASS_SAMPLE_COUNT) — useful with adaptive sampling (task 24).
- Per-lightgroup combined passes (with task 26).

## Plumbing
Channel→pass mapping lives in device/Frame.cpp:143-174 and pass creation in
Device.cpp:360-378 (currently fixed at device creation — consider creating passes lazily on
first channel request to avoid paying for unused passes).

## Acceptance
Each new channel maps and returns correct data types; unused channels cost nothing;
declared in cycles_device.json with descriptions.

## Suggested skills
`/tdd`; `/verify`; `/code-review`.
