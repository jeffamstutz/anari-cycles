/* SPDX-FileCopyrightText: 2011-2022 Blender Foundation
 *
 * SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "SpatialField.h"
// cycles
#include "scene/image.h"

namespace anari_cycles {

class VolumeImageLoader : public ccl::ImageLoader
{
 public:
  VolumeImageLoader(const StructuredRegularField *field_ptr);
  ~VolumeImageLoader();

  virtual bool load_metadata(ccl::ImageMetaData &metadata,
      const ccl::ImageLoaderParams &params,
      ccl::Progress &progress) override;

  virtual bool load_pixels(
      const ccl::ImageMetaData &metadata, void *pixels) override;

  virtual string name() const override;

  virtual bool equals(const ccl::ImageLoader &other) const override;

  virtual void cleanup() override;

  virtual bool is_vdb_loader() const override;

 protected:
  const StructuredRegularField *p_field;
};

} // namespace anari_cycles
