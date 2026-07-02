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
    pkg_check_modules(OpenColorIO REQUIRED IMPORTED_TARGET "OpenColorIO>=2.2")
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

set(WITH_LIBS_PRECOMPILED             OFF CACHE BOOL "")
set(WITH_CYCLES_ALEMBIC               OFF CACHE BOOL "")
set(WITH_CYCLES_DEVICE_CUDA           ${ANARI_CYCLES_USE_OPTIX} CACHE BOOL "")
set(WITH_CYCLES_DEVICE_HIP            OFF CACHE BOOL "")
set(WITH_CYCLES_DEVICE_OPTIX          ${ANARI_CYCLES_USE_OPTIX} CACHE BOOL "")
set(WITH_CYCLES_EMBREE                OFF CACHE BOOL "")
set(WITH_CYCLES_HYDRA_RENDER_DELEGATE OFF CACHE BOOL "")
set(WITH_CYCLES_NANOVDB               OFF CACHE BOOL "")
set(WITH_CYCLES_OPENIMAGEDENOISE      ${ANARI_CYCLES_USE_OIDN} CACHE BOOL "")
set(WITH_CYCLES_OPENSUBDIV            OFF CACHE BOOL "")
set(WITH_CYCLES_OPENVDB               OFF CACHE BOOL "")
set(WITH_CYCLES_OSL                   OFF CACHE BOOL "")
set(WITH_CYCLES_PUGIXML               ON  CACHE BOOL "")
set(WITH_CYCLES_USD                   OFF CACHE BOOL "")

add_subdirectory(cycles)

## ANARI device library ##

add_subdirectory(device)
