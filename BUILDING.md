# Building anari-cycles

Standard CMake build against the vendored Cycles sources:

```sh
cmake -S . -B <build-dir> [options...]
cmake --build <build-dir> -j
```

Required dependencies: ANARI SDK (>= 0.15), OpenImageIO, OpenColorIO (>= 2.2),
oneTBB. Point CMake at non-system installs with `CMAKE_PREFIX_PATH` (or
`TBB_ROOT`, `OpenColorIO_*` as needed).

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
