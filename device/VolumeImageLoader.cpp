// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "VolumeImageLoader.h"
// anari
#include "anari/anari_cpp.hpp"
// std
#include <algorithm>
#include <cstring>
#include <iostream>

namespace anari_cycles {

bool voxelToFloatSupported(ANARIDataType type)
{
  switch (type) {
  case ANARI_FLOAT32:
  case ANARI_FLOAT64:
  case ANARI_UFIXED8:
  case ANARI_UFIXED16:
  case ANARI_FIXED8:
  case ANARI_FIXED16:
    return true;
  default:
    return false;
  }
}

void convertVoxelsToFloat(
    ANARIDataType type, const void *src, size_t offset, float *dst, size_t n)
{
  switch (type) {
  case ANARI_FLOAT32:
    std::memcpy(dst, (const float *)src + offset, n * sizeof(float));
    break;
  case ANARI_FLOAT64: {
    const auto *s = (const double *)src + offset;
    for (size_t i = 0; i < n; ++i)
      dst[i] = float(s[i]);
    break;
  }
  case ANARI_UFIXED8: {
    const auto *s = (const uint8_t *)src + offset;
    for (size_t i = 0; i < n; ++i)
      dst[i] = s[i] / 255.f;
    break;
  }
  case ANARI_UFIXED16: {
    const auto *s = (const uint16_t *)src + offset;
    for (size_t i = 0; i < n; ++i)
      dst[i] = s[i] / 65535.f;
    break;
  }
  case ANARI_FIXED8: {
    const auto *s = (const int8_t *)src + offset;
    for (size_t i = 0; i < n; ++i)
      dst[i] = std::max(s[i] / 127.f, -1.f);
    break;
  }
  case ANARI_FIXED16: {
    const auto *s = (const int16_t *)src + offset;
    for (size_t i = 0; i < n; ++i)
      dst[i] = std::max(s[i] / 32767.f, -1.f);
    break;
  }
  default:
    break;
  }
}

VolumeImageLoader::VolumeImageLoader(
    Array3D *data, uint32_t tilesX, uint32_t tilesY)
    : m_data(data), m_tilesX(tilesX), m_tilesY(tilesY)
{
  if (m_data) {
    m_dims[0] = uint32_t(m_data->size(0));
    m_dims[1] = uint32_t(m_data->size(1));
    m_dims[2] = uint32_t(m_data->size(2));
  }
}

VolumeImageLoader::~VolumeImageLoader() = default;

bool VolumeImageLoader::load_metadata(
    ccl::ImageMetaData &metadata, const ccl::ImageLoaderParams &, ccl::Progress &)
{
  if (!m_data || !m_dims[0] || !m_dims[1] || !m_dims[2])
    return false;

  if (!voxelToFloatSupported(m_data->elementType())) {
    std::cerr << "Unsupported voxel data type "
              << anari::toString(m_data->elementType())
              << " for ANARI structuredRegular spatial field" << std::endl;
    return false;
  }

  if (m_tilesX * m_tilesY < m_dims[2])
    return false;

  metadata.type = ccl::IMAGE_DATA_TYPE_FLOAT;
  metadata.channels = 1;
  metadata.width = m_tilesX * m_dims[0];
  metadata.height = m_tilesY * m_dims[1];
  metadata.colorspace = ccl::u_colorspace_data;
  metadata.use_transform_3d = false;

  return true;
}

bool VolumeImageLoader::load_pixels(const ccl::ImageMetaData &, void *pixels)
{
  if (!m_data)
    return false;

  const auto type = m_data->elementType();
  if (!voxelToFloatSupported(type))
    return false;

  const size_t nx = m_dims[0];
  const size_t ny = m_dims[1];
  const size_t nz = m_dims[2];
  const size_t atlasW = size_t(m_tilesX) * nx;
  const size_t atlasH = size_t(m_tilesY) * ny;

  auto *dst = (float *)pixels;
  std::memset(dst, 0, atlasW * atlasH * sizeof(float));

  const void *src = m_data->data();
  for (size_t z = 0; z < nz; ++z) {
    const size_t tx = z % m_tilesX;
    const size_t ty = z / m_tilesX;
    for (size_t y = 0; y < ny; ++y) {
      const size_t srcOffset = (z * ny + y) * nx;
      float *dstRow = dst + (ty * ny + y) * atlasW + tx * nx;
      convertVoxelsToFloat(type, src, srcOffset, dstRow, nx);
    }
  }

  return true;
}

ccl::string VolumeImageLoader::name() const
{
  return "ANARI Volume";
}

bool VolumeImageLoader::equals(const ccl::ImageLoader &_other) const
{
  const auto *other = dynamic_cast<const VolumeImageLoader *>(&_other);
  if (!other)
    return false;
  return m_data == other->m_data && m_tilesX == other->m_tilesX
      && m_tilesY == other->m_tilesY;
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
