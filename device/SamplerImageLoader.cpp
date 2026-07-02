/* SPDX-FileCopyrightText: 2011-2022 Blender Foundation
 *
 * SPDX-License-Identifier: Apache-2.0 */

#include "SamplerImageLoader.h"
// anari
#include "anari/anari_cpp.hpp"

namespace anari_cycles {

namespace {

bool getCyclesImageType(ANARIDataType dataType, ccl::ImageDataType &cyclesType)
{
  switch (dataType) {
  case ANARI_UFIXED8:
  case ANARI_UFIXED8_R_SRGB:
    cyclesType = IMAGE_DATA_TYPE_BYTE;
    return true;
  case ANARI_UFIXED8_VEC2:
  case ANARI_UFIXED8_VEC3:
  case ANARI_UFIXED8_VEC4:
  case ANARI_UFIXED8_RA_SRGB:
  case ANARI_UFIXED8_RGB_SRGB:
  case ANARI_UFIXED8_RGBA_SRGB:
    cyclesType = IMAGE_DATA_TYPE_BYTE4;
    return true;
  case ANARI_UFIXED16:
    cyclesType = IMAGE_DATA_TYPE_USHORT;
    return true;
  case ANARI_UFIXED16_VEC2:
  case ANARI_UFIXED16_VEC3:
  case ANARI_UFIXED16_VEC4:
    cyclesType = IMAGE_DATA_TYPE_USHORT4;
    return true;
  case ANARI_FLOAT32:
    cyclesType = IMAGE_DATA_TYPE_FLOAT;
    return true;
  case ANARI_FLOAT32_VEC2:
  case ANARI_FLOAT32_VEC3:
  case ANARI_FLOAT32_VEC4:
    cyclesType = IMAGE_DATA_TYPE_FLOAT4;
    return true;
  default:
    return false;
  }
}

bool isSRGB(ANARIDataType dataType)
{
  return dataType == ANARI_UFIXED8_R_SRGB || dataType == ANARI_UFIXED8_RA_SRGB
      || dataType == ANARI_UFIXED8_RGB_SRGB
      || dataType == ANARI_UFIXED8_RGBA_SRGB;
}

} // namespace

SamplerImageLoader::SamplerImageLoader(Array1D *array) : m_array1d(array)
{
  m_dataType = array->elementType();
  m_dims[0] = uint32_t(array->totalSize());
  m_dims[1] = 1;
  m_pixels = array->data();
}

SamplerImageLoader::SamplerImageLoader(Array2D *array) : m_array2d(array)
{
  m_dataType = array->elementType();
  m_dims[0] = uint32_t(array->size(0));
  m_dims[1] = uint32_t(array->size(1));
  m_pixels = array->data();
}

SamplerImageLoader::~SamplerImageLoader() = default;

bool SamplerImageLoader::load_metadata(ccl::ImageMetaData &metadata,
    const ccl::ImageLoaderParams &,
    ccl::Progress &)
{
  if (!m_array1d && !m_array2d)
    return false;

  if (!getCyclesImageType(m_dataType, metadata.type)) {
    std::cerr << "Unsupported voxel data type " << anari::toString(m_dataType)
              << " for ANARI SamplerImageLoader" << std::endl;
    return false;
  }

  metadata.channels = anariComponentsOf(m_dataType);
  metadata.width = m_dims[0];
  metadata.height = m_dims[1];
  metadata.colorspace = ccl::u_colorspace_data;

  metadata.use_transform_3d = false;

  if (isSRGB(m_dataType))
    metadata.colorspace = ccl::u_colorspace_srgb;

  return true;
}

bool SamplerImageLoader::load_pixels(
    const ccl::ImageMetaData &metadata, void *pixels)
{
  if (!m_array1d && !m_array2d)
    return false;

  ccl::ImageDataType cyclesType;
  if (!getCyclesImageType(m_dataType, cyclesType))
    return false;

  auto bytes = m_dims[0] * m_dims[1] * m_dims[2] * anari::sizeOf(m_dataType);
  std::memcpy(pixels, m_pixels, bytes);
  metadata.conform_pixels(pixels);
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
  return m_array1d == other->m_array1d && m_array2d == other->m_array2d;
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
