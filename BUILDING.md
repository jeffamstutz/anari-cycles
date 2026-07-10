# Building anari-cycles

Standard CMake build against the vendored Cycles sources:

```sh
cmake -S . -B <build-dir> [options...]
cmake --build <build-dir> -j
```

Required dependencies: ANARI SDK (>= 0.15), OpenImageIO, OpenColorIO (>= 2.2),
oneTBB. Point CMake at non-system installs with `CMAKE_PREFIX_PATH` (or
`TBB_ROOT`, `OpenColorIO_*` as needed).

## GPU rendering (CUDA / OptiX)

```sh
cmake -DANARI_CYCLES_USE_OPTIX=ON \
      -DOPTIX_ROOT_DIR=<optix-sdk-root> \
      <build-dir>
```

- `ANARI_CYCLES_USE_OPTIX` drives Cycles' `WITH_CYCLES_DEVICE_CUDA`,
  `WITH_CYCLES_DEVICE_OPTIX`, and `WITH_CYCLES_CUDA_BINARIES` (all
  force-synced on reconfigure). Building needs the CUDA toolkit (`nvcc`) and
  the OptiX SDK headers (>= 8.0, `OPTIX_ROOT_DIR` is required); **neither is
  needed at runtime**.
- The GPU kernels are precompiled at build time and placed next to the plugin
  under `cycles/lib/` (`kernel_*.zst`) in both the build tree and the install
  tree (`<libdir>/cycles/lib`). The plugin locates them relative to its own
  path (`dladdr` + `ccl::path_init`), so installs are relocatable — keep the
  `cycles/lib` directory next to `libanari_library_cycles.so` when packaging.
  The Cycles kernel *source* tree does not need to be shipped.
- `ANARI_CYCLES_CUDA_ARCHS` (default `compute_75`) selects the kernel
  architectures. The default is a single PTX that covers every Turing+ GPU:
  the driver JIT-compiles it on first use (slow first render, cached by the
  driver; OptiX likewise JIT-compiles and caches per GPU). Add `sm_XX`
  entries (cubins) for instant startup on known GPUs in distribution builds —
  each arch costs minutes of nvcc time.
- Diagnostics: set `ANARI_CYCLES_LOG_LEVEL=info` (fatal/error/warning/info/
  debug/trace) to see Cycles' kernel resolution ("Using precompiled kernel")
  and other internal logs on stdout/stderr.

## Denoising (KHR_RENDERER_DENOISE)

The renderer's `denoise` parameter needs a denoiser compiled into Cycles.
Without one, the parameter (and the extension declaration) still exists, but
the device warns `denoise requested but no denoiser compiled in` and renders
un-denoised.

### OpenImageDenoise (CPU, recommended)

```sh
cmake -DANARI_CYCLES_USE_OIDN=ON \
      -DOPENIMAGEDENOISE_ROOT_DIR=<oidn-install-prefix> \
      <build-dir>
```

- `ANARI_CYCLES_USE_OIDN` drives Cycles' `WITH_CYCLES_OPENIMAGEDENOISE`
  (force-synced on every reconfigure — setting the Cycles variable directly is
  overwritten) and defines `WITH_OPENIMAGEDENOISE` for the device sources.
- `OPENIMAGEDENOISE_ROOT_DIR` is the hint variable honored by Cycles' bundled
  `FindOpenImageDenoise.cmake` (a CMake cache variable or an environment
  variable). Tested with OIDN 2.3 (the vendored Cycles supports 1.x and 2.x).

## VDB volumes (KHR_SPATIAL_FIELD_NANOVDB, structuredRegular filter="cubic")

The `nanovdb` spatial field subtype and tricubic filtering on
`structuredRegular` fields use Cycles' native VDB volume path:

```sh
cmake -DWITH_CYCLES_OPENVDB=ON \
      -DWITH_CYCLES_NANOVDB=ON \
      -DOPENVDB_ROOT_DIR=<openvdb-install-prefix> \
      <build-dir>
```

- Both flags are required together: NanoVDB itself is header-only, but
  Cycles' VDB image pipeline (`scene/image_vdb.cpp`) hard-requires the
  OpenVDB *library* — its `WITH_NANOVDB` code path builds the NanoVDB images
  the kernel samples out of OpenVDB grids. The top-level CMakeLists enforces
  this pairing.
- The NanoVDB headers are searched next to OpenVDB
  (`NANOVDB_ROOT_DIR`/`OPENVDB_ROOT_DIR`, cache or environment variables).
  When not found — some distro OpenVDB packages omit them — they are fetched
  automatically (FetchContent, network access required at configure time)
  from the OpenVDB v11.0.0 release, which carries the NanoVDB 32.6 headers
  this tree was tested against. When the fetch kicks in and the local
  OpenVDB is a different major release, configure emits a version-skew
  warning; if the build then fails in NanoVDB/OpenVDB conversion code, set
  `NANOVDB_ROOT_DIR` to the NanoVDB headers matching your OpenVDB release.
- Without VDB support, `nanovdb` fields warn and yield an invalid field, and
  `filter="cubic"` warns and falls back to `linear`.
- Tested with OpenVDB 11.0.1 / NanoVDB 32.6.

### OptiX denoiser

When the OptiX backend is enabled (`-DANARI_CYCLES_USE_OPTIX=ON`), the OptiX
denoiser is available as well; the device selects it automatically when
rendering on an OptiX device, and falls back to OIDN (when also enabled)
otherwise.

### Runtime note

The build-tree `libanari_library_cycles.so` carries an RPATH to the OIDN lib
directory, so in-tree testing needs no extra environment. Installed copies use
an `$ORIGIN`-relative RPATH only — place the OIDN runtime libraries
(`libOpenImageDenoise.so.2`, `libOpenImageDenoise_core.so.*`, and the
`libOpenImageDenoise_device_*.so.*` modules) next to the installed library, or
add the OIDN lib directory to `LD_LIBRARY_PATH`.
