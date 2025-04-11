## Copyright 2025 Jefferson Amstutz
## SPDX-License-Identifier: Apache-2.0

## Establish Project ##

project(anari_library_cycles LANGUAGES CXX)

## Dependencies ##

find_package(anari 0.13.0 REQUIRED)

## Cycles definitions ##

add_definitions(
  -DCCL_NAMESPACE_BEGIN=namespace\ ccl\ {
  -DCCL_NAMESPACE_END=}
)

## Build device library ##

add_library(${PROJECT_NAME} SHARED)

include(GenerateExportHeader)
generate_export_header(${PROJECT_NAME}
  EXPORT_MACRO_NAME "CYCLES_DEVICE_INTERFACE"
)

target_sources(${PROJECT_NAME}
PRIVATE
  CyclesGlobalState.cpp
  Camera.cpp
  Device.cpp
  Frame.cpp
  FrameDisplayDriver.cpp
  FrameOutputDriver.cpp
  Geometry.cpp
  Group.cpp
  ImageLoader.cpp
  Instance.cpp
  Library.cpp
  Light.cpp
  Material.cpp
  Object.cpp
  Renderer.cpp
  SpatialField.cpp
  Surface.cpp
  Volume.cpp
  World.cpp
)

get_target_property(CYCLES_INCLUDE_DIRECTORIES cycles_bvh INCLUDE_DIRECTORIES)

target_include_directories(${PROJECT_NAME}
PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CYCLES_INCLUDE_DIRECTORIES}
  ${CMAKE_SOURCE_DIR}/cycles/src
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)

target_link_libraries(${PROJECT_NAME}
PRIVATE
  anari::helium
  anari::anari
  cycles_device
  cycles_kernel
  cycles_scene
  cycles_session
  cycles_bvh
  cycles_subd
  cycles_graph
  cycles_util
  extern_sky
  OpenImageIO::OpenImageIO
)

## ANARI query code generation ##

anari_generate_queries(
  DEVICE_TARGET ${PROJECT_NAME}
  CPP_NAMESPACE anari_cycles
  JSON_DEFINITIONS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cycles_device.json
)

## Installation ##

install(TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
