# Fix: intermittent segfaults / black frames (scene-mutation vs render-session race)

**Type:** P0 bugfix (observed, root cause unknown)
**Context:** discovered during task 01 verification (2026-07-03); reproduced on unmodified main

## Problem
~25% of offline test runs against the device segfault (exit 139/134) mid-render, and
occasionally a frame renders fully black. Reproduced with small ANARI programs that render
several frames/scenes in sequence (see the lighting test pattern in `.todo/README.md`).
Suspected race between ANARI-side scene mutation (commit/finalize touching `ccl::Scene`)
and the asynchronous Cycles render session (`session->start()` in device/Frame.cpp:91-141;
scene rebuild in device/World.cpp:73-97 `setCyclesWorldObjects` clears/rebuilds
`scene->objects` wholesale).

## Goal
Diagnose and fix. Likely needs proper session pause/cancel or scene-lock around
commit-time mutation (Cycles `Session` has `set_pause`, `cancel`, `wait`;
`scene->mutex` exists for this). Then run a stress loop (50+ renders) to confirm.

## Acceptance
50 consecutive runs of a multi-frame test program with zero crashes and zero unexpected
black frames; `anariRenderTests` full suite stable across 5 repeats.

## Suggested skills
`/diagnosing-bugs`; `/verify`; `/code-review`.
