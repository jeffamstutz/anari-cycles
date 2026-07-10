# Build: precompile GPU kernels; remove runtime kernel-source dependency

**Type:** build/deployment fix — OptiX/CUDA builds currently require the Cycles kernel
*source tree* (plus nvcc and the OptiX SDK) on disk at runtime, in a location the plugin
cannot control. Goal: kernels precompiled at build time, located relative to the plugin
itself. Slow first-run JIT (driver/OptiX PTX compile) is acceptable; loose source files
and a runtime toolchain are not.
**Context:** investigation completed 2026-07-09 (this doc is the handoff; no other artifact).
Cycles snapshot: 5.2.0 (`cycles/src/util/version.h`).

## Findings from the investigation (do not re-derive)

Runtime kernel search order, `CUDADevice::compile_kernel`
(`cycles/src/device/cuda/device_impl.cpp:248-318,340-409`):
1. precompiled cubin `path_get("lib/kernel_sm_<M><m>.cubin.zst")`, walking minor down;
2. precompiled PTX `path_get("lib/kernel_compute_XX.ptx.zst")`, walking down to compute_50
   (driver JITs these for newer GPUs, cached by the driver);
3. previously JIT-built cubin in `~/.cache/cycles/kernels/` — keyed by an MD5 of the
   installed **source** tree, so even the cache hit needs `path_get("source")` to exist;
4. runtime `nvcc` on `<source>/kernel/device/cuda/kernel.cu` (needs source + CUDA toolkit).

OptiX (`cycles/src/device/optix/device_impl.cpp:239-263`): looks for
`lib/kernel_optix[_shader_raytrace|_osl*].ptx.zst`; if missing (or adaptive-compile), it
needs OptiX SDK headers (`OPTIX_ROOT_DIR` env or baked `CYCLES_RUNTIME_OPTIX_ROOT_DIR`)
plus kernel source to JIT. OptiX always JIT-compiles the PTX per-GPU at
`optixModuleCreate` (own disk cache), so precompiled PTX already gives "slow first run,
no source" for free. OptiX also loads the CUDA utility kernels (search order above).

Why nothing is found today — three independent gaps:
1. **No precompiled kernels are built.** `ANARI_CYCLES_USE_OPTIX` only forces
   `WITH_CYCLES_DEVICE_CUDA/OPTIX` (CMakeLists.txt:170-171). The master switch
   `WITH_CYCLES_CUDA_BINARIES` (cycles/CMakeLists.txt:149) defaults OFF and is never set.
2. **The search root is the host executable's directory, not the plugin's.** Nothing under
   `device/` calls `ccl::path_init()`; `path_get()` then falls back to
   `path_dirname(OIIO::Sysutil::this_program_path())` (`cycles/src/util/path.cpp:356-368`)
   — i.e. wherever anariViewer/ParaView/etc. lives. Env overrides
   (`path.cpp:308-326`): `CYCLES_KERNEL_PATH` redirects only `"source"`; **there is no
   override for `"lib"`** — only `path_init` controls it.
3. **Nothing installs kernel artifacts next to the plugin.** `device/CMakeLists.txt:172-175`
   installs only the plugin target. Cycles' own `delayed_do_install()`
   (`cycles/src/CMakeLists.txt:408-409`) puts `lib/*.zst` at `${CMAKE_INSTALL_PREFIX}/lib`
   and the source tree at `${CMAKE_INSTALL_PREFIX}/source` — lining up with the
   executable-relative fallback only by accident.

Build side, when `WITH_CYCLES_CUDA_BINARIES=ON`
(`cycles/src/kernel/device/cuda/CMakeLists.txt:74-235`): per arch in
`CYCLES_CUDA_BINARIES_ARCH`, nvcc emits `kernel_<arch>.cubin`/`.ptx`, zstd-compresses to
`.zst`, delayed-installs to `${CYCLES_INSTALL_PATH}/lib`. The arch loop (lines 164-210)
auto-skips arches the detected CUDA version can't target (e.g. CUDA 13 dropped sm_50-70).
nvcc is found via `find_package(CUDA)` (`cycles/src/cmake/external_libs.cmake:751`).
OptiX kernels (`cycles/src/kernel/device/optix/CMakeLists.txt:39-140`) are gated on
`WITH_CYCLES_CUDA_BINARIES AND WITH_CYCLES_DEVICE_OPTIX` plus `OPTIX_ROOT_DIR`, and build
`kernel_optix.ptx.zst` + `kernel_optix_shader_raytrace.ptx.zst` (+ `_osl*` variants when
OSL is on). Each arch costs minutes of nvcc time — trim the list.

## Chosen design (no Cycles source changes)

1. **Wire up precompilation** in the top-level `CMakeLists.txt`, alongside the existing
   FORCE block at lines 167-172: when `ANARI_CYCLES_USE_OPTIX=ON`, force
   `WITH_CYCLES_CUDA_BINARIES=ON` and fail with a clear message if `OPTIX_ROOT_DIR` is
   unset. Override `CYCLES_CUDA_BINARIES_ARCH` with a trimmed default — `compute_75`
   (one PTX, covers every Turing+ GPU via driver JIT) plus optionally the developer's
   native `sm_XX` for instant startup; expose as a cache var (e.g.
   `ANARI_CYCLES_CUDA_ARCHS`) so users can widen it for distribution builds.
2. **Make the plugin self-locating.** At device/library init (once — e.g.
   `device/Library.cpp`), resolve the plugin's own path via `dladdr()` on a symbol in the
   library and call `ccl::path_init(<derived root>, "")`. Suggested layout: root =
   `<plugin dir>/cycles`, so artifacts live at `<plugin dir>/cycles/lib/*.zst` (clean
   namespacing; `path_get("lib")` = root + `/lib`). Linux-only is fine for v1; leave a
   `GetModuleHandleExA` TODO for Windows.
3. **Co-locate the artifacts.** Copy the built `.zst` files (from the
   `cycles/src/kernel/device/{cuda,optix}` binary dirs) into
   `${CMAKE_BINARY_DIR}/cycles/lib` so the build tree works as-is, and add install rules
   placing them at `<plugin install dir>/cycles/lib`. The `cuda_cubins` list isn't visible
   at top level — either read Cycles' delayed-install global properties after
   `add_subdirectory(cycles)`, or copy from the known binary dirs via a custom target.
4. Optional belt-and-braces: bake `CYCLES_RUNTIME_OPTIX_ROOT_DIR` at configure time so
   runtime JIT still works for an arch not covered by the shipped PTX (needs source tree;
   should never trigger with `compute_75` shipped).

With all three in place the `source/` install tree is no longer needed for GPU rendering
and there is no runtime nvcc/OptiX-SDK dependency.

## Future step (separate task; consider after this lands)

**Fully self-contained plugin (zero loose files):** Cycles has no upstream option to embed
kernels in the binary, but the runtime already reads each `.zst` into memory and hands a
blob to `cuModuleLoadData` / `optixModuleCreateWithTasks`, so the hook points are narrow:
embed the compressed blobs as byte arrays (objcopy / CMake-generated header) and
short-circuit the two `path_get("lib/...")` lookups in
`cycles/src/device/cuda/device_impl.cpp` and `cycles/src/device/optix/device_impl.cpp`
(~50 lines). Cost: a permanent local patch to re-apply on every Cycles snapshot update.
Only worth it if shipping the `cycles/lib/` directory next to the plugin proves to be a
real deployment problem.

## Touch points

`CMakeLists.txt` (force `WITH_CYCLES_CUDA_BINARIES`, arch list, `OPTIX_ROOT_DIR` check),
`device/CMakeLists.txt` (copy + install kernel artifacts), `device/Library.cpp` (dladdr +
`ccl::path_init`), `BUILDING.md` (document `OPTIX_ROOT_DIR` requirement and the new
layout), `IMPLEMENTATION_STATUS.md` if it tracks GPU-build caveats.

## Acceptance

Configure with `-DANARI_CYCLES_USE_OPTIX=ON -DOPTIX_ROOT_DIR=<sdk>`: build produces
`cycles/lib/kernel_compute_75.ptx.zst` and `kernel_optix*.ptx.zst` next to the plugin in
both build and install trees. From an installed copy with `${prefix}/source` deleted and
no CUDA toolkit on PATH, a GPU render via `~/opt/anari/bin/anariViewer` run from an
unrelated cwd succeeds; Cycles logs "Using precompiled kernel" (visible at info log
level). CPU-only builds and `anariCts` results vs `cts-baseline.md` are unaffected.

## Suggested skills

`/verify` (drive anariViewer from a relocated install with the source tree removed);
`/code-review` before merging.
