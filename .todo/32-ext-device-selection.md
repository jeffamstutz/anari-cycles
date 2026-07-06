# Vendor ext: explicit compute-device selection (CPU/CUDA/OptiX/HIP/Metal/oneAPI)

**Type:** vendor extension — deployment usability
**Context:** `../IMPLEMENTATION_STATUS.md` §4.2 · Cycles: `cycles/src/device/device.h:38-97`

## Current state
Device auto-selects OptiX → CUDA → CPU at construction (device/Device.cpp:290-312);
`ANARI_CYCLES_FORCE_CPU` env var is the only override. Cycles supports CPU, CUDA, OptiX,
HIP(+HIPRT), Metal, oneAPI, and multi-device (`DeviceType`, device.h:38; `DeviceInfo`
enumeration per session).

## Proposed ANARI surface
ANARI_DEVICE parameters (set before first commit):
- `computeDevice` (STRING: auto/cpu/cuda/optix/hip/metal/oneapi)
- `computeDeviceIndex` (INT32, default 0) for multi-GPU hosts
- Read-only device properties listing available backends
  (e.g. `computeDevices` STRING_LIST) for UI pickers.
Keep the env var as an override for containers/CI; document precedence.

## Constraint
Cycles session/device is created in `Device` construction — parameters on an ANARI device
arrive after `anariNewDevice` but before first use; move session creation to first
commit/first frame if necessary (check `CyclesGlobalState` init in Device.cpp:290-386).

## Acceptance
`computeDevice="cpu"` on a GPU host renders on CPU (verify via Cycles log/timings);
invalid selection warns and falls back to auto; properties enumerate correctly.

## Suggested skills
`/tdd`; `/verify`; `/code-review`.
