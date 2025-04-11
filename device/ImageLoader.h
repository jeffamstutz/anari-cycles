/* SPDX-FileCopyrightText: 2011-2022 Blender Foundation
 *
 * SPDX-License-Identifier: Apache-2.0 */

#ifndef __IMAGE_ANARI__
#define __IMAGE_ANARI__

#include "SpatialField.h"
#include "scene/image.h"

namespace anari_cycles {

class ANARIImageLoader : public ImageLoader {
 public:
  ANARIImageLoader(const StructuredRegularField *field_ptr);
  ~ANARIImageLoader();

  virtual bool load_metadata(const ImageDeviceFeatures &features,
                             ImageMetaData &metadata) override;

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

#endif /* __IMAGE_ANARI__ */
