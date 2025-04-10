## Copyright 2025 Jefferson Amstutz
## SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

## Language setup ##

set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

set(CMAKE_CXX_STANDARD 17)
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

## Dependencies ##

find_package(anari 0.13.0 REQUIRED)
find_package(Python3 REQUIRED)
find_package(OpenImageIO REQUIRED)

## Cycles library ##

set(WITH_CYCLES_ALEMBIC               OFF CACHE BOOL "")
set(WITH_CYCLES_DEVICE_CUDA           OFF CACHE BOOL "")
set(WITH_CYCLES_DEVICE_HIP            OFF CACHE BOOL "")
set(WITH_CYCLES_DEVICE_OPTIX          OFF CACHE BOOL "")
set(WITH_CYCLES_EMBREE                OFF CACHE BOOL "")
set(WITH_CYCLES_HYDRA_RENDER_DELEGATE OFF CACHE BOOL "")
set(WITH_CYCLES_NANOVDB               OFF CACHE BOOL "")
set(WITH_CYCLES_OPENCOLORIO           OFF CACHE BOOL "")
set(WITH_CYCLES_OPENIMAGEDENOISE      OFF CACHE BOOL "")
set(WITH_CYCLES_OPENSUBDIV            OFF CACHE BOOL "")
set(WITH_CYCLES_OPENVDB               OFF CACHE BOOL "")
set(WITH_CYCLES_OSL                   OFF CACHE BOOL "")
set(WITH_CYCLES_PUGIXML               ON  CACHE BOOL "")
set(WITH_CYCLES_USD                   OFF CACHE BOOL "")

add_subdirectory(cycles EXCLUDE_FROM_ALL)

## ANARI device library ##

add_subdirectory(device)
