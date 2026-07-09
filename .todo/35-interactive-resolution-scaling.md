# Vendor ext: low-res preview frames during interaction (Blender-style)

**Type:** vendor extension (CYCLES_RENDERER_INTERACTIVE_SCALING) — device-side emulation of
Cycles' viewport resolution divider to fix slow interaction rates.
**Context:** investigation completed 2026-07-09 (this doc is the handoff; no other artifact).
Cycles reference: `cycles/src/integrator/render_scheduler.cpp`, `cycles/src/integrator/path_trace.cpp`.

## Findings from the investigation (do not re-derive)

Blender's fast interaction comes from `SessionParams::use_resolution_divider`: on every
`Session::reset()` the `RenderScheduler` restarts at `start_resolution_divider_`
(default `pixel_size * 8`, auto-tuned to ~1/30 s per update, long axis clamped ≥ 128 px —
`render_scheduler.cpp:1152-1198`), renders `min(divider, 4)` throwaway samples into a
divider-scaled buffer (`scale_buffer_params()`, `path_trace.cpp:332`), then steps the divider
to 1 and restarts sampling from zero (`render_scheduler.cpp:323-341`).

The device disables this (`use_resolution_divider = false`, device/Device.cpp:515) and
**cannot simply enable it** — three blockers, all verified in source:

1. **Delivery path.** The device consumes results only via `FrameOutputDriver`
   (an `OutputDriver`); `write_render_tile()` fires only when the scheduler is `done()`,
   which requires divider == 1 AND sample target reached (`render_scheduler.cpp:281-292, 373`).
   A single `anariRenderFrame` after a camera move would get *slower* (low-res passes rendered
   and discarded first) and the app would never see the previews.
2. **`OutputDriver::update_render_tile()` is unusable during the low-res phase.** It is called
   for intermediate updates (`path_trace.cpp:757-762`), but the `Tile` reports full-res
   dimensions (`get_render_tile_size()`, `path_trace.cpp:1208-1217`) while `get_pass_pixels()`
   writes divider-scaled pixel counts from the effective buffers (`path_trace_work.cpp:134-144`).
   The Tile API has no effective-resolution field; only the `DisplayDriver` API communicates the
   scaled size (`path_trace.cpp:773-779`). Upstream only exercises `update_render_tile` in
   background renders where the divider is forced to 1 (`render_scheduler.cpp:131`).
3. **Sample bookkeeping.** The device maps each `anariRenderFrame` to a cumulative sample
   interval via `set_samples()` (device/frame/Frame.cpp:348-351) and derives
   `renderProgress`/`refinementProgress` from session Progress counters; the scheduler zeroes
   `num_rendered_samples` at every divider hop, scrambling those properties.

A native adoption (CPU `DisplayDriver` + display-update-driven frame completion) was
considered and rejected as a large architectural change that fights ANARI frame semantics.

## Chosen design: device-side divider emulation (no Cycles source changes)

- The navigation signal already exists: `Frame::resetAccumulationNextFrame()`
  (device/frame/Frame.cpp:555) is true exactly when scene/camera changed.
- On reset frames (previews): set `state.buffer_params` *and* the camera
  (`m_camera->setCameraCurrent(...)`, called in the reset block Frame.cpp:282-346) to
  `size / divider`, render normally — a completed small render, so `tile.size` is honest and
  blocker #2 does not apply.
- `FrameOutputDriver::write_render_tile()` currently rejects size-mismatched tiles
  (device/frame/FrameOutputDriver.cpp:168-178); instead, when a preview tile arrives,
  upscale extracted passes into the full-res frame buffers (nearest for depth/ids;
  nearest or bilinear for color/normal/albedo/lightgroup/aux).
- On the next `renderFrame` with no changes: detect "last frame was a preview" and force one
  internal full-res `session->reset()` restarting accumulation from sample 0 (mirrors the
  scheduler's divider step-down). Refinement then proceeds as today.
- Divider selection: reuse Cycles' heuristic shape — target ~1/30 s from the measured previous
  frame duration (`m_duration` already tracked in FrameOutputDriver::renderEnd), clamp long
  axis ≥ 128 px, cap ~8. A fixed-divider override param is fine for v1.

### Gating / exposure decisions (already settled)
- Engage only when frame `accumulation` is true — with it off, every frame resets and would be
  a low-res preview forever.
- Opt-in vendor renderer parameter(s) under a new `CYCLES_RENDERER_INTERACTIVE_SCALING`
  feature: enable flag + optional fixed divider / target frame time. Declare in
  `device/cycles_device.json` (code-gen, CMakeLists.txt:93-97) alongside existing vendor exts.
  Default off → CTS baseline (`cts-baseline.md`) and existing behavior untouched.

### Known caveats (accepted; document, don't fight)
- All channels are upscaled on preview frames — depth/objectId/primitiveId blocky; picking
  during navigation is approximate (Blender has the same property).
- First frame after a change returns an upscaled preview instead of a full-res 1-sample image
  (app-visible; hence opt-in).
- Preview frames change buffer dimensions → render-buffer realloc per transition; dwarfed by
  the ~divider² pixel savings.
- Denoise interplay is favorable: preview is a normal small render so the denoise path applies;
  Cycles prefers 1 sample + denoise at higher res during navigation
  (`render_scheduler.cpp:985-991`) — worth mirroring when renderer `denoise` is on.
- `renderProgress`/`duration` property semantics for preview frames need a look
  (Frame.cpp:197-228); keep them monotone/sane.
- Orthogonal: the `runAsync` perf note at Frame.cpp:356-362 — previews shorten the blocking
  window but don't solve async; out of scope here.

## Touch points
`device/frame/Frame.{h,cpp}` (reset-path sizing, preview-state tracking, forced full-res
reset), `device/frame/FrameOutputDriver.{h,cpp}` (accept + upscale preview tiles),
`device/renderer/Renderer.{h,cpp}` (params), `device/cycles_device.json`,
`IMPLEMENTATION_STATUS.md` (new extension entry).

## Acceptance
Params visible in `anariInfo` with descriptions; with the feature on and accumulation on, a
camera-move frame completes ~divider²× faster and the image visibly fills in over subsequent
frames (verify in `~/opt/anari/bin/anariViewer`); with the feature off (default), rendered
output is bit-identical to today and `anariCts` matches `cts-baseline.md`; ids/depth channels
correct again on the first full-res frame after interaction stops.

## Suggested skills
`/tdd` (behavioral test: preview frame duration + fill-in); `/verify` (drive anariViewer);
`/code-review` before merging.
