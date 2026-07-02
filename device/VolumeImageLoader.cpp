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
    ImageMetaData &, const ImageLoaderParams &, Progress &)
{
  // Cycles removed dense 3D image textures. Structured volumes need a
  // NanoVDB-backed loader rather than copying voxels into a 2D image buffer.
  return false;
}

bool VolumeImageLoader::load_pixels(const ImageMetaData &, void *)
{
  return false;
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
