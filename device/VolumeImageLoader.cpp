/* SPDX-FileCopyrightText: 2011-2022 Blender Foundation
 *
 * SPDX-License-Identifier: Apache-2.0 */

#include "VolumeImageLoader.h"
// anari
#include "anari/anari_cpp.hpp"

namespace anari_cycles {

VolumeImageLoader::VolumeImageLoader(const StructuredRegularField *field_ptr)
    : p_field(field_ptr)
{}

VolumeImageLoader::~VolumeImageLoader() = default;

bool VolumeImageLoader::load_metadata(
    const ImageDeviceFeatures &features, ImageMetaData &metadata)
{
  metadata.byte_size = p_field->m_data->totalSize()
      * anari::sizeOf(p_field->m_data->elementType());
  metadata.channels = 1;
  metadata.transform_3d =
      ccl::transform_scale(ccl::make_float3(1.f / p_field->m_dims[0],
          1.f / p_field->m_dims[1],
          1.f / p_field->m_dims[2]));
  metadata.use_transform_3d = true;

  metadata.width = p_field->m_dims[0];
  metadata.height = p_field->m_dims[1];
  metadata.depth = p_field->m_dims[2];

  switch (p_field->m_data->elementType()) {
  case (ANARI_UFIXED8):
    metadata.type = IMAGE_DATA_TYPE_BYTE;
    break;
  case (ANARI_UFIXED16):
    metadata.type = IMAGE_DATA_TYPE_USHORT;
    break;
  case (ANARI_FLOAT32):
    metadata.type = IMAGE_DATA_TYPE_FLOAT;
    break;
  case (ANARI_FIXED16):
  case (ANARI_FLOAT64):
  case (ANARI_UNKNOWN):
    // TODO throw error
    std::cerr << "Unsupported voxel data type\n";
    return false;
  }

  return true;
}

bool VolumeImageLoader::load_pixels(
    const ImageMetaData &, void *pixels, const size_t, const bool)
{
  auto size = p_field->m_data->totalSize()
      * anari::sizeOf(p_field->m_data->elementType());
  memcpy(pixels, p_field->m_data->data(), size);
  return true;
}

string VolumeImageLoader::name() const
{
  return "ANARI Volume";
}

bool VolumeImageLoader::equals(const ImageLoader &other) const
{
  // TODO
  return false;
}

void VolumeImageLoader::cleanup()
{
  // no-op
}

bool VolumeImageLoader::is_vdb_loader() const
{
  return false;
}

} // namespace anari_cycles
