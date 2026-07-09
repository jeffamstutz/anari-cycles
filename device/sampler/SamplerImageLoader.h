/* SPDX-FileCopyrightText: 2011-2022 Blender Foundation
 *
 * SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "array/Array1D.h"
#include "array/Array2D.h"
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
  // The loader can outlive the sampler that created it (it is owned by the
  // Cycles image slot, and ImageManager::add_image() dedupes new images
  // against every live slot via equals()). Pin the source array so the
  // pointer identity that equals() relies on stays valid -- otherwise a
  // freed array reallocated at the same address dedupes onto a stale image.
  helium::IntrusivePtr<Array1D> m_array1d;
  helium::IntrusivePtr<Array2D> m_array2d;

  anari::DataType m_dataType{ANARI_UNKNOWN};
  uint3 m_dims{1, 1, 1};
  const void *m_pixels{nullptr};
};

} // namespace anari_cycles
