/* SPDX-FileCopyrightText: 2011-2022 Blender Foundation
 *
 * SPDX-License-Identifier: Apache-2.0 */

#include "SamplerImageLoader.h"
// anari
#include "anari/anari_cpp.hpp"

namespace anari_cycles {

SamplerImageLoader::SamplerImageLoader(Array2D *array) : m_array2d(array)
{
  m_dataType = array->elementType();
  m_dims[0] = uint32_t(array->size(0));
  m_dims[1] = uint32_t(array->size(1));
  m_pixels = array->data();
}

SamplerImageLoader::~SamplerImageLoader() = default;

bool SamplerImageLoader::load_metadata(
    const ccl::ImageDeviceFeatures &features, ccl::ImageMetaData &metadata)
{
  metadata.byte_size =
      m_dims[0] * m_dims[1] * m_dims[2] * anari::sizeOf(m_dataType);
  metadata.channels = anariComponentsOf(m_dataType);
  metadata.use_transform_3d = false;

  metadata.width = m_dims[0];
  metadata.height = m_dims[1];
  metadata.depth = m_dims[2];

  switch (m_dataType) {
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
    std::cerr << "Unsupported voxel data type " << anari::toString(m_dataType)
              << " for ANARI SamplerImageLoader" << std::endl;
    return false;
  }

  return true;
}

bool SamplerImageLoader::load_pixels(
    const ccl::ImageMetaData &, void *pixels, const size_t, const bool)
{
  auto bytes = m_dims[0] * m_dims[1] * m_dims[2] * anari::sizeOf(m_dataType);
  std::memcpy(pixels, m_pixels, bytes);
  return true;
}

ccl::string SamplerImageLoader::name() const
{
  return "ANARI Sampler";
}

bool SamplerImageLoader::equals(const ccl::ImageLoader &_other) const
{
  const auto *other = dynamic_cast<const SamplerImageLoader *>(&_other);
  if (!other)
    return false;
  return m_array2d == other->m_array2d;
}

void SamplerImageLoader::cleanup()
{
  // no-op
}

bool SamplerImageLoader::is_vdb_loader() const
{
  return false;
}

} // namespace anari_cycles
