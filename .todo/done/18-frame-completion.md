# Feature: frame completion — channels, completion callback, progress, accumulation

**Type:** spec completeness
**Depends on:** 05-introspection-json-sync.md (declares the already-working channels)
**Context:** `../IMPLEMENTATION_STATUS.md` §2.3 · registry: `khr_frame_*.json`

## Gaps
- **`channel.primitiveId` / `channel.instanceId`**: missing. Cycles has no built-in prim-id
  pass; instanceId can come from per-object `pass_id`/`random_id`
  (cycles/src/scene/object.h:45-65) — currently `pass_id` is used for Surface `id`
  (device/Group.cpp:44), so decide the id-plumbing split (objectId spec semantics = Surface/
  Volume `id`, instanceId = Instance `id`; audit found instance `id` is not read at all —
  device/Instance.cpp:16-21).
- **KHR_FRAME_COMPLETION_CALLBACK**: `frameCompletionCallback`(+UserData) params —
  fire when a frame finishes. Hook `FrameOutputDriver::renderEnd`
  (device/FrameOutputDriver.cpp:84-97). Mind threading: callback must be invoked per spec
  (any thread allowed, but document).
- **`renderProgress` / `refinementProgress` frame properties**: Cycles `Progress` (via
  `session->progress`) has render/sample progress; wire into `Frame::getProperty`
  (device/Frame.cpp:68-89).
- **KHR_FRAME_ACCUMULATION**: behavior already exists (1 sample per renderFrame,
  `set_samples(++sessionSamples)`, Frame.cpp:131) — add the `accumulation` BOOL param
  (when false, each renderFrame should restart accumulation) and declare the extension.

## Acceptance
`anariInfo` lists all supported channels; a test app receives the completion callback;
progress property increases monotonically during a long render; CTS frame tests improve.

## Suggested skills
`/tdd`; `/verify`; `/code-review`.
