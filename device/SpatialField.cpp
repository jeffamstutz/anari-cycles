// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

// std
#include <cfloat>
// ours
// #include "Array.h"
#include "ImageLoader.h"
#include "SpatialField.h"
// cycles
#include "scene/volume.h"
#include "util/hash.h"

namespace anari_cycles {

SpatialField::SpatialField(CyclesGlobalState *s)
    : Object(ANARI_SPATIAL_FIELD, s)
{}

SpatialField::~SpatialField() = default;

SpatialField *SpatialField::createInstance(
    std::string_view subtype, CyclesGlobalState *s)
{
  if (subtype == "structuredRegular")
    return new StructuredRegularField(s);
  else
    return (SpatialField *)new UnknownObject(ANARI_SPATIAL_FIELD, subtype, s);
}

void SpatialField::finalize()
{
  Object::finalize();
}

// Subtypes ///////////////////////////////////////////////////////////////////

// StructuredRegularField //

StructuredRegularField::StructuredRegularField(CyclesGlobalState *s)
    : SpatialField(s)
{}

void StructuredRegularField::commitParameters()
{
  m_data = getParamObject<helium::Array3D>("data");
  m_origin = getParam<helium::float3>("origin", helium::float3(0.f));
  m_spacing = getParam<helium::float3>("spacing", helium::float3(1.f));
}

void StructuredRegularField::finalize()
{
  if (!m_data) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'data' on 'structuredRegular' field");
    return;
  }
  m_dims = m_data->size();
  m_coordUpperBound = helium::float3(std::nextafter(m_dims[0] - 1, 0),
      std::nextafter(m_dims[1] - 1, 0),
      std::nextafter(m_dims[2] - 1, 0));

  SpatialField::finalize();
}

bool StructuredRegularField::isValid() const
{
  return m_data;
}

std::unique_ptr<ccl::Geometry> StructuredRegularField::makeCyclesGeometry()
{
  auto volume = std::make_unique<ccl::Volume>();
  volume->name = ccl::ustring("ANARI Volume");

  volume->set_clipping(-FLT_MAX);
  volume->set_object_space(true);
#if 0
  volume->set_volume_mesh(true);
#endif

  Attribute *attr = volume->attributes.add(
      ustring("voxels"), TypeDesc::TypeFloat, ATTR_ELEMENT_VOXEL);
  auto loader = std::make_unique<ANARIImageLoader>(this);
  ImageParams params;
  auto &state = *deviceState();
  attr->data_voxel() =
      state.scene->image_manager->add_image(std::move(loader), params, false);

  auto v_min = make_float3(0.5, 0.5f, 0.5f);
  auto v_max =
      make_float3(m_dims[0] - 0.5f, m_dims[1] - 0.5f, m_dims[2] - 0.5f);
  auto vertices = std::vector<float3>{{v_min.x, v_min.y, v_max.z},
      {v_max.x, v_min.y, v_max.z},
      {v_min.x, v_max.y, v_max.z},
      {v_max.x, v_max.y, v_max.z},
      {v_min.x, v_min.y, v_min.z},
      {v_max.x, v_min.y, v_min.z},
      {v_min.x, v_max.y, v_min.z},
      {v_max.x, v_max.y, v_min.z}};
  ccl::array<ccl::float3> P;
  P.resize(8);
  std::copy(cbegin(vertices), cend(vertices), P.begin());
  volume->set_verts(P);

  auto faces = std::vector<int3>{{0, 1, 2},
      {2, 1, 3},
      {1, 5, 3},
      {3, 5, 7},
      {5, 4, 7},
      {7, 4, 6},
      {4, 0, 6},
      {6, 0, 2},
      {2, 3, 6},
      {6, 3, 7},
      {5, 4, 1},
      {1, 4, 0}};
  auto numTriangles = faces.size();
  volume->reserve_mesh(numTriangles * 3, numTriangles);
  for (const auto &f : faces) {
    volume->add_triangle(f.x, f.y, f.z, 0, true);
  }

  std::vector<float3> face_normals;
  for (const auto &f : faces) {
    auto v1 = vertices[f.x];
    auto v2 = vertices[f.y];
    auto v3 = vertices[f.z];
    auto e1 = normalize(v2 - v1);
    auto e2 = normalize(v3 - v1);

    face_normals.push_back(cross(e1, e2));
  }

#if 0
  Attribute *attr_fN = volume->attributes.add(ATTR_STD_FACE_NORMAL);
  float3 *fN = attr_fN->data_float3();
  for (size_t i = 0; i < face_normals.size(); ++i) {
    fN[i] = face_normals[i];
  }
#endif

  return volume;
}

box3 StructuredRegularField::bounds() const
{
  if (!isValid())
    return empty_box3();

  box3 b;
  b.lower[0] = m_origin[0];
  b.lower[1] = m_origin[1];
  b.lower[2] = m_origin[2];

  b.upper[0] = (m_dims[0] - 1.f) * m_spacing[0];
  b.upper[1] = (m_dims[1] - 1.f) * m_spacing[1];
  b.upper[2] = (m_dims[2] - 1.f) * m_spacing[2];

  return b;
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::SpatialField *);
