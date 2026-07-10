## Copyright 2025 Jefferson Amstutz
## SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

## Language setup ##

set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_BUILD_TYPE_INIT Release)

set(CMAKE_INSTALL_RPATH "$ORIGIN")
set(CMAKE_INSTALL_MESSAGE LAZY)

## Top-level project ##

project(anari-cycles LANGUAGES C CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

include(CMakePrintHelpers)
include(GNUInstallDirs)

option(ANARI_CYCLES_USE_OPTIX "Use NVIDIA OptiX for GPU rendering" OFF)
option(ANARI_CYCLES_USE_OIDN "Use OpenImageDenoise for CPU denoising" OFF)

## Dependencies ##

find_package(anari 0.15.0 REQUIRED)
find_package(Python3 REQUIRED)
find_package(OpenImageIO REQUIRED)
find_package(OpenColorIO 2.2 CONFIG QUIET)

if(NOT TARGET OpenColorIO::OpenColorIO)
  if(NOT OpenColorIO_INCLUDE_DIR OR NOT OpenColorIO_LIBRARY)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(OpenColorIO IMPORTED_TARGET "OpenColorIO>=2.2")
    if(NOT OpenColorIO_FOUND)
      message(FATAL_ERROR
        "OpenColorIO >= 2.2 was not found (via CMake config or pkg-config). "
        "Cycles requires it even with all optional features disabled. "
        "Install OpenColorIO 2.2+ and add its install prefix to "
        "CMAKE_PREFIX_PATH, or set OpenColorIO_INCLUDE_DIR and "
        "OpenColorIO_LIBRARY manually."
      )
    endif()
    set(OpenColorIO_DEPENDENCY PkgConfig::OpenColorIO)
  endif()

  add_library(OpenColorIO::OpenColorIO INTERFACE IMPORTED)
  if(OpenColorIO_DEPENDENCY)
    set_property(TARGET OpenColorIO::OpenColorIO
      PROPERTY INTERFACE_LINK_LIBRARIES ${OpenColorIO_DEPENDENCY})
  else()
    set_target_properties(OpenColorIO::OpenColorIO PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES ${OpenColorIO_INCLUDE_DIR}
      INTERFACE_LINK_LIBRARIES "${OpenColorIO_LIBRARY};${OpenColorIO_DEPENDENCIES}"
    )
  endif()
endif()

set(USD_OVERRIDE_OPENCOLORIO ON)

## Cycles library ##

# Cycles feature toggles. Everything defaults to OFF for a dependency-free
# first configure; enable features individually with -DWITH_CYCLES_<X>=ON
# (each one pulls in the corresponding external dependency).
set(WITH_LIBS_PRECOMPILED             OFF CACHE BOOL "Use Blender's precompiled library dependencies")
set(WITH_CYCLES_ALEMBIC               OFF CACHE BOOL "Alembic procedural support (requires Alembic)")
set(WITH_CYCLES_DEVICE_HIP            OFF CACHE BOOL "AMD HIP device support")
set(WITH_CYCLES_EMBREE                OFF CACHE BOOL "Embree CPU ray tracing (requires Embree >= 3.8)")
set(WITH_CYCLES_HYDRA_RENDER_DELEGATE OFF CACHE BOOL "Hydra render delegate (requires USD)")
set(WITH_CYCLES_NANOVDB               OFF CACHE BOOL "NanoVDB volume support (requires NanoVDB)")
set(WITH_CYCLES_OPENSUBDIV            OFF CACHE BOOL "Subdivision surfaces (requires OpenSubdiv)")
set(WITH_CYCLES_OPENVDB               OFF CACHE BOOL "OpenVDB volume support (requires OpenVDB)")
set(WITH_CYCLES_OSL                   OFF CACHE BOOL "Open Shading Language support (requires OSL)")
set(WITH_CYCLES_PUGIXML               ON  CACHE BOOL "XML support (requires pugixml)")
set(WITH_CYCLES_USD                   OFF CACHE BOOL "USD scene support (requires USD)")

# NanoVDB volume support ('nanovdb' spatial fields + tricubic filtering).
# NanoVDB itself is header-only, but Cycles' VDB image pipeline hard-requires
# the OpenVDB *library* as well (scene/image_vdb.cpp only compiles its
# WITH_NANOVDB path against OpenVDB grids), so WITH_CYCLES_NANOVDB=ON must be
# paired with WITH_CYCLES_OPENVDB=ON (found via -DOPENVDB_ROOT_DIR=<prefix>).
# The NanoVDB headers usually ship with OpenVDB; when they are not installed
# (some distro OpenVDB packages omit them), fetch the headers from the OpenVDB
# release carrying the NanoVDB version this Cycles snapshot was tested with.
if(WITH_CYCLES_NANOVDB)
  if(NOT WITH_CYCLES_OPENVDB)
    message(FATAL_ERROR
      "WITH_CYCLES_NANOVDB=ON requires WITH_CYCLES_OPENVDB=ON: Cycles "
      "builds its NanoVDB volume images from OpenVDB grids."
    )
  endif()
  find_path(NANOVDB_INCLUDE_DIR
    NAMES nanovdb/NanoVDB.h
    HINTS ${NANOVDB_ROOT_DIR} ENV NANOVDB_ROOT_DIR
          ${OPENVDB_ROOT_DIR} ENV OPENVDB_ROOT_DIR
    PATH_SUFFIXES include
  )
  if(NOT NANOVDB_INCLUDE_DIR)
    include(FetchContent)
    # Deliberately the direct (multi-argument) FetchContent_Populate form:
    # this downloads and extracts only — the OpenVDB superproject must never
    # be add_subdirectory'd, we only want its nanovdb/ headers. (CMP0169
    # deprecates the single-argument Declare+Populate form, not this one.)
    FetchContent_Populate(nanovdb_headers
      URL https://github.com/AcademySoftwareFoundation/openvdb/archive/refs/tags/v11.0.0.tar.gz
      URL_HASH SHA256=6314ff1db057ea90050763e7b7d7ed86d8224fcd42a82cdbb9c515e001b96c74
      SOURCE_DIR ${CMAKE_BINARY_DIR}/_deps/nanovdb_headers-src
    )
    # Header-only use: just put the fetched nanovdb/ directory on the include
    # path (never add_subdirectory the OpenVDB superproject).
    set(NANOVDB_INCLUDE_DIR
      ${CMAKE_BINARY_DIR}/_deps/nanovdb_headers-src/nanovdb
      CACHE PATH "NanoVDB include directory (fetched headers)" FORCE
    )
    message(STATUS "NanoVDB headers not found locally; fetched to ${NANOVDB_INCLUDE_DIR}")

    # Version-skew guard: the fetched headers are NanoVDB 32.6 (from OpenVDB
    # 11). If the OpenVDB found locally is a different major release, the
    # NanoVDB<->OpenVDB conversion headers may not match the library that
    # will be linked. This is usually a compile-time problem (not a silent
    # runtime one), so warn with the fix rather than hard-failing.
    set(_anari_cycles_fetched_openvdb_major 11)
    set(_anari_cycles_openvdb_version_h "")
    foreach(_prefix ${OPENVDB_ROOT_DIR} $ENV{OPENVDB_ROOT_DIR}
        ${CMAKE_PREFIX_PATH} /usr /usr/local)
      if(_prefix AND EXISTS "${_prefix}/include/openvdb/version.h")
        set(_anari_cycles_openvdb_version_h "${_prefix}/include/openvdb/version.h")
        break()
      endif()
    endforeach()
    if(_anari_cycles_openvdb_version_h)
      file(STRINGS "${_anari_cycles_openvdb_version_h}" _anari_cycles_openvdb_major_line
        REGEX "#define OPENVDB_LIBRARY_MAJOR_VERSION_NUMBER +[0-9]+"
      )
      if(_anari_cycles_openvdb_major_line MATCHES "([0-9]+)")
        if(NOT CMAKE_MATCH_1 EQUAL _anari_cycles_fetched_openvdb_major)
          message(WARNING
            "Fetched NanoVDB headers come from OpenVDB "
            "${_anari_cycles_fetched_openvdb_major}.x but the local OpenVDB "
            "(${_anari_cycles_openvdb_version_h}) is major version "
            "${CMAKE_MATCH_1}. If the build fails in NanoVDB/OpenVDB "
            "conversion code, point NANOVDB_ROOT_DIR at the NanoVDB headers "
            "matching your OpenVDB release instead."
          )
        endif()
      endif()
    endif()
    unset(_anari_cycles_fetched_openvdb_major)
    unset(_anari_cycles_openvdb_version_h)
    unset(_anari_cycles_openvdb_major_line)
  endif()
endif()

# These are derived from the ANARI_CYCLES_USE_* options above, so keep them in
# sync on reconfigure (FORCE); without it a pre-existing cache entry wins and
# toggling the ANARI_CYCLES_USE_* option silently does nothing.
set(WITH_CYCLES_DEVICE_CUDA      ${ANARI_CYCLES_USE_OPTIX} CACHE BOOL "" FORCE)
set(WITH_CYCLES_DEVICE_OPTIX     ${ANARI_CYCLES_USE_OPTIX} CACHE BOOL "" FORCE)
set(WITH_CYCLES_OPENIMAGEDENOISE ${ANARI_CYCLES_USE_OIDN}  CACHE BOOL "" FORCE)

# GPU builds precompile the CUDA/OptiX kernels at build time so the installed
# plugin has no runtime dependency on the Cycles kernel source tree, nvcc, or
# the OptiX SDK. The artifacts (kernel_*.zst) land next to the plugin under
# cycles/lib/ (see device/CMakeLists.txt); device/Library.cpp points Cycles'
# path lookup at that directory.
set(WITH_CYCLES_CUDA_BINARIES ${ANARI_CYCLES_USE_OPTIX} CACHE BOOL "" FORCE)
if(ANARI_CYCLES_USE_OPTIX)
  # Building needs OptiX headers (>= 8.0) only — no SDK libraries. Use
  # -DOPTIX_ROOT_DIR=<path> if you have the SDK or the optix-dev headers
  # locally; otherwise the headers are fetched from github.com/NVIDIA/optix-dev.
  include(cmake/FetchOptiXHeaders.cmake)

  # Trimmed architecture list (Cycles' default builds a dozen cubins, minutes
  # of nvcc time each). compute_75 PTX alone covers every Turing+ GPU: the
  # driver JIT-compiles it on first use and caches the result. Add sm_XX
  # entries (e.g. sm_86) for instant startup on specific GPUs in
  # distribution builds.
  set(ANARI_CYCLES_CUDA_ARCHS compute_75 CACHE STRING
    "CUDA architectures to precompile Cycles kernels for (compute_XX = PTX, sm_XX = cubin)"
  )
  set(CYCLES_CUDA_BINARIES_ARCH ${ANARI_CYCLES_CUDA_ARCHS} CACHE STRING "" FORCE)

  # Belt-and-braces: bake the SDK path in so runtime JIT (adaptive compile, or
  # an arch somehow not covered by the shipped kernels) can still find OptiX
  # headers without the OPTIX_ROOT_DIR environment variable. This path never
  # needs to exist on end-user machines.
  if(NOT CYCLES_RUNTIME_OPTIX_ROOT_DIR)
    set(CYCLES_RUNTIME_OPTIX_ROOT_DIR ${OPTIX_ROOT_DIR} CACHE PATH "" FORCE)
  endif()
endif()

# EXCLUDE_FROM_ALL keeps Cycles' install rules (standalone app, kernel
# sources, headers, licenses) out of this project's install; only the ANARI
# device is installed. The Cycles libraries still build as link dependencies
# of the device, but the standalone 'cycles' app no longer builds by default.
add_subdirectory(cycles EXCLUDE_FROM_ALL)

# Cycles finds TBB headers and library independently, so a CMAKE_PREFIX_PATH
# holding several TBB copies can pair legacy (2020) headers with a oneTBB
# (2021+) library. That configures fine but leaves unresolvable legacy task
# symbols in every Cycles binary, so fail loudly here instead.
if(TBB_INCLUDE_DIR AND TBB_LIBRARY AND EXISTS "${TBB_INCLUDE_DIR}/tbb/tbb_stddef.h")
  file(REAL_PATH "${TBB_LIBRARY}" _anari_cycles_tbb_library)
  if(_anari_cycles_tbb_library MATCHES "libtbb\\.so\\.12")
    message(FATAL_ERROR
      "Mismatched TBB: legacy (<= 2020) headers at ${TBB_INCLUDE_DIR} but "
      "oneTBB library ${TBB_LIBRARY}. Point both at one install, e.g. "
      "-DTBB_ROOT=/usr for the system oneTBB."
    )
  endif()
  unset(_anari_cycles_tbb_library)
endif()

## ANARI device library ##

add_subdirectory(device)
