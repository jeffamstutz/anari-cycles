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
  Device.cpp
  Library.cpp
  Object.cpp
  array/Array1D.cpp
  array/ObjectArray.cpp
  camera/Camera.cpp
  camera/Omnidirectional.cpp
  camera/Orthographic.cpp
  camera/Perspective.cpp
  frame/Frame.cpp
  frame/FrameOutputDriver.cpp
  geometry/Curve.cpp
  geometry/Geometry.cpp
  geometry/Isosurface.cpp
  geometry/MarchingCubes.cpp
  geometry/Mesh.cpp
  geometry/Sphere.cpp
  geometry/Tube.cpp
  light/Directional.cpp
  light/HDRI.cpp
  light/Light.cpp
  light/Point.cpp
  light/QuadLight.cpp
  light/Ring.cpp
  light/Sky.cpp
  light/Spot.cpp
  light/UnknownLight.cpp
  material/HairMaterial.cpp
  material/Material.cpp
  material/MatteMaterial.cpp
  material/OSLMaterial.cpp
  material/PhysicallyBasedMaterial.cpp
  material/ToonMaterial.cpp
  renderer/Renderer.cpp
  sampler/Image1D.cpp
  sampler/Image2D.cpp
  sampler/Sampler.cpp
  sampler/SamplerImageLoader.cpp
  spatial_field/NanoVDBField.cpp
  spatial_field/SpatialField.cpp
  spatial_field/StructuredRegularField.cpp
  spatial_field/VolumeImageLoader.cpp
  surface/Surface.cpp
  volume/FieldVolume.cpp
  volume/PrincipledVolume.cpp
  volume/TransferFunction1D.cpp
  volume/Volume.cpp
  world/Group.cpp
  world/Instance.cpp
  world/World.cpp
)

get_target_property(CYCLES_INCLUDE_DIRECTORIES cycles_bvh INCLUDE_DIRECTORIES)

target_include_directories(${PROJECT_NAME}
PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CYCLES_INCLUDE_DIRECTORIES}
  ${CMAKE_SOURCE_DIR}/cycles/src
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-msse4.2" ANARI_CYCLES_HAS_SSE42)
if(ANARI_CYCLES_HAS_SSE42)
  target_compile_options(${PROJECT_NAME} PRIVATE -msse4.2)
endif()

if(ANARI_CYCLES_USE_OPTIX)
  target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_OPTIX)
endif()
if(ANARI_CYCLES_USE_OIDN)
  target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_OPENIMAGEDENOISE)
endif()
if(WITH_CYCLES_OPENSUBDIV)
  # Lets the device warn when 'catmullClark' subdivision would silently
  # degrade to linear dicing in a build without OpenSubdiv.
  target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_OPENSUBDIV)
endif()
if(WITH_CYCLES_OPENVDB)
  # Must match the Cycles build: scene/image_vdb.h (included by
  # spatial_field/) changes layout with these defines.
  target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_OPENVDB)
  target_include_directories(${PROJECT_NAME} PRIVATE ${OPENVDB_INCLUDE_DIR})
endif()
if(WITH_CYCLES_NANOVDB)
  # Enables the native VDB volume path: the 'nanovdb' spatial field subtype
  # (KHR_SPATIAL_FIELD_NANOVDB) and structuredRegular filter="cubic"
  # (KHR_SPATIAL_FIELD_STRUCTURED_REGULAR_CUBIC). Both also need
  # WITH_CYCLES_OPENVDB (Cycles' VDBImageLoader converts through OpenVDB);
  # without VDB support the subtype warns and yields an invalid field, and
  # cubic falls back to linear.
  target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_NANOVDB)
  target_include_directories(${PROJECT_NAME} PRIVATE ${NANOVDB_INCLUDE_DIR})
endif()
if(WITH_CYCLES_OSL)
  # Enables the CYCLES_MATERIAL_OSL runtime path ('osl' material subtype,
  # material/OSLMaterial.cpp) and the session-wide OSL shading system
  # switch in Device.cpp. Without it the subtype warns and yields an
  # invalid material.
  target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_OSL)
  # scene/osl.h pulls in OSL (and its OpenImageIO/Imath) headers.
  target_include_directories(${PROJECT_NAME} PRIVATE ${OSL_INCLUDE_DIR})
endif()

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

# device/json/ holds local copies of registry extensions (stems prefixed with
# 'cycles_') trimmed to the parameters this device actually implements; they
# take the place of the SDK registry files for the stems referenced by
# cycles_device.json.
file(GLOB LOCAL_EXTENSION_JSONS CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/json/*.json
)

anari_generate_queries(
  DEVICE_TARGET ${PROJECT_NAME}
  CPP_NAMESPACE anari_cycles
  JSON_DEFINITIONS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cycles_device.json
  JSON_ROOT_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/json
  JSON_EXTENSION_FILES "${LOCAL_EXTENSION_JSONS}"
)

## Installation ##

install(TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
