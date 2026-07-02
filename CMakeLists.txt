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

# These are derived from the ANARI_CYCLES_USE_* options above, so keep them in
# sync on reconfigure (FORCE); without it a pre-existing cache entry wins and
# toggling the ANARI_CYCLES_USE_* option silently does nothing.
set(WITH_CYCLES_DEVICE_CUDA      ${ANARI_CYCLES_USE_OPTIX} CACHE BOOL "" FORCE)
set(WITH_CYCLES_DEVICE_OPTIX     ${ANARI_CYCLES_USE_OPTIX} CACHE BOOL "" FORCE)
set(WITH_CYCLES_OPENIMAGEDENOISE ${ANARI_CYCLES_USE_OIDN}  CACHE BOOL "" FORCE)

add_subdirectory(cycles)

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
