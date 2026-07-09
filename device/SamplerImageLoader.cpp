/* SPDX-FileCopyrightText: 2011-2022 Blender Foundation
 *
 * SPDX-License-Identifier: Apache-2.0 */

#include "SamplerImageLoader.h"
// anari
#include "anari/anari_cpp.hpp"

namespace anari_cycles {

namespace {

// All formats are uploaded as 4-channel Cycles images: the kernel reads 1-
// and 2-channel images as grayscale(+alpha), but ANARI wants the attribute
// conversion rules ((v,0,0,1), (r,g,0,1), (r,0,0,a) for RA). Missing
// components are filled in load_pixels() instead. UFIXED32 has no Cycles
// storage type and is converted to float.
bool getCyclesImageType(ANARIDataType dataType, ccl::ImageDataType &cyclesType)
{
  switch (dataType) {
  case ANARI_UFIXED8:
  case ANARI_UFIXED8_VEC2:
  case ANARI_UFIXED8_VEC3:
  case ANARI_UFIXED8_VEC4:
  case ANARI_UFIXED8_R_SRGB:
  case ANARI_UFIXED8_RA_SRGB:
  case ANARI_UFIXED8_RGB_SRGB:
  case ANARI_UFIXED8_RGBA_SRGB:
    cyclesType = IMAGE_DATA_TYPE_BYTE4;
    return true;
  case ANARI_UFIXED16:
  case ANARI_UFIXED16_VEC2:
  case ANARI_UFIXED16_VEC3:
  case ANARI_UFIXED16_VEC4:
    cyclesType = IMAGE_DATA_TYPE_USHORT4;
    return true;
  case ANARI_FLOAT32:
  case ANARI_FLOAT32_VEC2:
  case ANARI_FLOAT32_VEC3:
  case ANARI_FLOAT32_VEC4:
  case ANARI_UFIXED32:
  case ANARI_UFIXED32_VEC2:
  case ANARI_UFIXED32_VEC3:
  case ANARI_UFIXED32_VEC4:
    cyclesType = IMAGE_DATA_TYPE_FLOAT4;
    return true;
  default:
    return false;
  }
}

bool isUFixed32(ANARIDataType dataType)
{
  return dataType == ANARI_UFIXED32 || dataType == ANARI_UFIXED32_VEC2
      || dataType == ANARI_UFIXED32_VEC3 || dataType == ANARI_UFIXED32_VEC4;
}

bool isSRGB(ANARIDataType dataType)
{
  return dataType == ANARI_UFIXED8_R_SRGB || dataType == ANARI_UFIXED8_RA_SRGB
      || dataType == ANARI_UFIXED8_RGB_SRGB
      || dataType == ANARI_UFIXED8_RGBA_SRGB;
}

// Expand packed 'channels'-component texels to 4 components with the ANARI
// attribute fill rules: missing components are 0, missing alpha is 1. The RA
// formats hold (red, alpha) pairs, expanded to (r, 0, 0, a).
template <typename T>
void expandTexels(const void *src_,
    T *dst,
    size_t count,
    int channels,
    bool redAlpha,
    T one)
{
  const T *src = static_cast<const T *>(src_);
  for (size_t i = 0; i < count; i++, src += channels, dst += 4) {
    dst[0] = src[0];
    dst[1] = (!redAlpha && channels > 1) ? src[1] : T(0);
    dst[2] = channels > 2 ? src[2] : T(0);
    dst[3] = channels > 3 ? src[3] : (redAlpha ? src[1] : one);
  }
}

} // namespace

SamplerImageLoader::SamplerImageLoader(Array1D *array) : m_array1d(array)
{
  m_dataType = array->elementType();
  // region-aware (KHR_ARRAY1D_REGION): only [begin, end) becomes the image
  m_dims[0] = uint32_t(array->size());
  m_dims[1] = 1;
  m_pixels = array->begin();
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

  // load_pixels() always fills all four components itself
  metadata.channels = 4;
  metadata.width = m_dims[0];
  metadata.height = m_dims[1];
  metadata.colorspace = ccl::u_colorspace_data;

  metadata.use_transform_3d = false;

  // scene_linear_srgb, not u_colorspace_srgb: the latter makes
  // ImageMetaData::finalize() upgrade BYTE4 storage to HALF4 for an OCIO
  // conversion, mismatching the byte pixels load_pixels() writes.
  if (isSRGB(m_dataType))
    metadata.colorspace = ccl::u_colorspace_scene_linear_srgb;

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

  const size_t count = size_t(m_dims[0]) * m_dims[1] * m_dims[2];
  const int channels = anariComponentsOf(m_dataType);
  const bool redAlpha = m_dataType == ANARI_UFIXED8_RA_SRGB;

  if (isUFixed32(m_dataType)) {
    // Cycles has no 32-bit unsigned-normalized image type; convert to float
    // while expanding.
    const auto *src = static_cast<const uint32_t *>(m_pixels);
    auto *dst = static_cast<float *>(pixels);
    constexpr double scale = 1.0 / double(UINT32_MAX);
    for (size_t i = 0; i < count; i++, src += channels, dst += 4) {
      dst[0] = float(src[0] * scale);
      dst[1] = channels > 1 ? float(src[1] * scale) : 0.f;
      dst[2] = channels > 2 ? float(src[2] * scale) : 0.f;
      dst[3] = channels > 3 ? float(src[3] * scale) : 1.f;
    }
  } else if (cyclesType == IMAGE_DATA_TYPE_BYTE4) {
    expandTexels<uint8_t>(m_pixels,
        static_cast<uint8_t *>(pixels),
        count,
        channels,
        redAlpha,
        uint8_t(0xffu));
  } else if (cyclesType == IMAGE_DATA_TYPE_USHORT4) {
    expandTexels<uint16_t>(m_pixels,
        static_cast<uint16_t *>(pixels),
        count,
        channels,
        redAlpha,
        uint16_t(0xffffu));
  } else {
    expandTexels<float>(
        m_pixels, static_cast<float *>(pixels), count, channels, false, 1.f);
  }

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
  // m_dims/m_pixels participate so a re-finalized sampler whose source array
  // changed its 'region' (KHR_ARRAY1D_REGION) does not dedupe onto the image
  // built from the old region.
  return m_array1d.ptr == other->m_array1d.ptr
      && m_array2d.ptr == other->m_array2d.ptr
      && m_dataType == other->m_dataType && m_dims[0] == other->m_dims[0]
      && m_dims[1] == other->m_dims[1] && m_dims[2] == other->m_dims[2]
      && m_pixels == other->m_pixels;
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
