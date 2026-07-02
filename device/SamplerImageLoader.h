/* SPDX-FileCopyrightText: 2011-2022 Blender Foundation
 *
 * SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "Array.h"
#include "cycles_math.h"
// helium
#include "helium/utility/IntrusivePtr.h"
// cycles
#include "scene/image.h"

namespace anari_cycles {

class SamplerImageLoader : public ccl::ImageLoader
{
 public:
  SamplerImageLoader(Array1D *array);
  SamplerImageLoader(Array2D *array);
  SamplerImageLoader(Array3D *array);
  ~SamplerImageLoader();

  virtual bool load_metadata(ccl::ImageMetaData &metadata,
      const ccl::ImageLoaderParams &params,
      ccl::Progress &progress) override;
  virtual bool load_pixels(
      const ccl::ImageMetaData &metadata, void *pixels) override;
  virtual ccl::string name() const override;
  virtual bool equals(const ccl::ImageLoader &other) const override;
  virtual void cleanup() override;
  virtual bool is_vdb_loader() const override;

 private:
  Array1D *m_array1d{nullptr};
  Array2D *m_array2d{nullptr};
  Array3D *m_array3d{nullptr};

  anari::DataType m_dataType{ANARI_UNKNOWN};
  uint3 m_dims{1, 1, 1};
  const void *m_pixels{nullptr};
};

} // namespace anari_cycles
