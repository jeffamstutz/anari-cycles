## Copyright 2025 Jefferson Amstutz
## SPDX-License-Identifier: Apache-2.0

# Provide OptiX headers when OPTIX_ROOT_DIR was not given.
#
# OptiX 7+ is a header-only API: the implementation lives in the NVIDIA
# display driver (loaded at runtime by optix_stubs.h via optixInit()), and
# Cycles links no OptiX library — its FindOptiX.cmake only looks for optix.h,
# and the kernel precompile step only needs -I<headers>. NVIDIA publishes
# exactly those headers at https://github.com/NVIDIA/optix-dev, so the full
# OptiX SDK is not required to build.
#
# Pinned to v8.0.0: the oldest release this project supports. OptiX headers
# determine the ABI version requested from the driver at runtime, so the
# oldest working headers maximize driver compatibility. Override with
# -DOPTIX_ROOT_DIR=<path> to use a local SDK or a different header version.
#
# Note: the headers are covered by NVIDIA's proprietary SDK license (see
# LICENSE.txt in the fetched tree) — same terms as the SDK download, only
# acquisition differs.

if(OPTIX_ROOT_DIR)
  return()
endif()

include(FetchContent)
# Direct (multi-argument) FetchContent_Populate form: download and extract
# only, never add_subdirectory (the repo is just headers plus license files).
FetchContent_Populate(optix_headers
  URL https://github.com/NVIDIA/optix-dev/archive/refs/tags/v8.0.0.tar.gz
  URL_HASH SHA256=b32e74c9f5c13549ff3a9760076271b5b6ec28f93fe6a8dd0bde74d7e5c58e05
  SOURCE_DIR ${CMAKE_BINARY_DIR}/_deps/optix_headers-src
)

# The repo root holds include/optix.h, which is the layout Cycles'
# FindOptiX.cmake expects for OPTIX_ROOT_DIR (PATH_SUFFIXES include).
set(OPTIX_ROOT_DIR
  ${CMAKE_BINARY_DIR}/_deps/optix_headers-src
  CACHE PATH "OptiX include root (fetched headers)" FORCE
)
message(STATUS "OPTIX_ROOT_DIR not set; fetched OptiX 8.0.0 headers to ${OPTIX_ROOT_DIR}")
