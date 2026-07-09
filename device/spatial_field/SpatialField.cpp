// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "SpatialField.h"
// subtypes
#include "NanoVDBField.h"
#include "StructuredRegularField.h"
// cycles
#include "scene/geometry.h"
#include "scene/scene.h"
#include "scene/shader_nodes.h"
// std
#include <atomic>
#include <string>

namespace anari_cycles {

SpatialField::SpatialField(CyclesGlobalState *s)
    : Object(ANARI_SPATIAL_FIELD, s)
{
  static std::atomic<uint64_t> s_nextFieldIndex{0};
  m_voxelAttributeName = ccl::ustring(
      "ANARI_spatial_field_" + std::to_string(s_nextFieldIndex++));
}

SpatialField::~SpatialField() = default;

SpatialField *SpatialField::createInstance(
    std::string_view subtype, CyclesGlobalState *s)
{
  if (subtype == "structuredRegular")
    return new StructuredRegularField(s);
  else if (subtype == "nanovdb")
    return new NanoVDBField(s);
  else
    return (SpatialField *)new UnknownObject(ANARI_SPATIAL_FIELD, subtype, s);
}

void SpatialField::finalize()
{
  Object::finalize();
}

void SpatialField::attachVoxelAttributes(ccl::Geometry *geom) const
{
  if (m_voxelImage.empty())
    return;
  auto *attr = geom->attributes.add(
      m_voxelAttributeName, ccl::TypeFloat, ccl::ATTR_ELEMENT_VOXEL);
  attr->data_voxel_for_write() = m_voxelImage;
}

ccl::ShaderOutput *SpatialField::createVoxelSamplingNodes(
    ccl::ShaderGraph *graph)
{
  if (m_voxelImage.empty())
    return nullptr;
  auto *attr = graph->create_node<ccl::AttributeNode>();
  attr->set_attribute(m_voxelAttributeName);
  return attr->output("Fac");
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::SpatialField *);
