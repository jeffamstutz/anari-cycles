/* SPDX-FileCopyrightText: 2011-2022 Blender Foundation
 *
 * SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "SpatialField.h"
// cycles
#include "scene/image.h"

namespace anari_cycles {

class VolumeImageLoader : public ImageLoader
{
 public:
  VolumeImageLoader(const StructuredRegularField *field_ptr);
  ~VolumeImageLoader();

  virtual bool load_metadata(
      const ImageDeviceFeatures &features, ImageMetaData &metadata) override;

  virtual bool load_pixels(const ImageMetaData &metadata,
      void *pixels,
      const size_t pixels_size,
      const bool associate_alpha) override;

  virtual string name() const override;

  virtual bool equals(const ImageLoader &other) const override;

  virtual void cleanup() override;

  virtual bool is_vdb_loader() const override;

 protected:
  const StructuredRegularField *p_field;
};

} // namespace anari_cycles
