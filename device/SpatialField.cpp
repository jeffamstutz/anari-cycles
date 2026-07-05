// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

// std
#include <algorithm>
#include <cmath>
// ours
#include "SpatialField.h"
#include "VolumeImageLoader.h"
// cycles
#include "scene/scene.h"
#include "scene/shader_nodes.h"

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
    : SpatialField(s), m_data(this)
{}

StructuredRegularField::~StructuredRegularField() = default;

void StructuredRegularField::commitParameters()
{
  m_data = getParamObject<helium::Array3D>("data");
  m_origin = getParam<helium::float3>("origin", helium::float3(0.f));
  m_spacing = getParam<helium::float3>("spacing", helium::float3(1.f));
  m_linearFilter = getParamString("filter", "linear") != "nearest";
}

void StructuredRegularField::finalize()
{
  if (!m_data) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'data' on 'structuredRegular' field");
    m_atlas = ccl::ImageHandle();
    SpatialField::finalize();
    return;
  }

  m_dims = m_data->size();
  if (!isValid()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'data' on 'structuredRegular' field has a zero-sized dimension");
    m_atlas = ccl::ImageHandle();
    SpatialField::finalize();
    return;
  }

  // Tile the Z slices into a roughly square 2D atlas so large volumes stay
  // within practical 2D texture dimensions.
  const uint32_t nz = m_dims[2];
  const float idealTilesX =
      std::sqrt(float(nz) * float(m_dims[1]) / float(std::max(m_dims[0], 1u)));
  m_tilesX = std::min(std::max(uint32_t(std::lround(idealTilesX)), 1u), nz);
  m_tilesY = (nz + m_tilesX - 1) / m_tilesX;

  auto &state = *deviceState();
  auto loader =
      std::make_unique<VolumeImageLoader>(m_data.get(), m_tilesX, m_tilesY);
  ccl::ImageParams params;
  params.alpha_type = IMAGE_ALPHA_AUTO;
  params.colorspace = ccl::u_colorspace_data;
  params.interpolation =
      m_linearFilter ? INTERPOLATION_LINEAR : INTERPOLATION_CLOSEST;
  m_atlas =
      state.scene->image_manager->add_image(std::move(loader), params, false);

  SpatialField::finalize();
}

bool StructuredRegularField::isValid() const
{
  return m_data && m_dims[0] > 0 && m_dims[1] > 0 && m_dims[2] > 0;
}

ccl::ShaderOutput *StructuredRegularField::createCyclesSamplingNodes(
    ccl::ShaderGraph *graph)
{
  if (!isValid() || m_atlas.empty())
    return nullptr;

  const float nx = float(m_dims[0]);
  const float ny = float(m_dims[1]);
  const float nz = float(m_dims[2]);
  const float atlasW = float(m_tilesX) * nx;
  const float atlasH = float(m_tilesY) * ny;

  const auto b = bounds();
  const float3 lower = make_float3(b.lower[0], b.lower[1], b.lower[2]);
  const float3 size = ccl::max(
      make_float3(b.upper[0], b.upper[1], b.upper[2]) - lower,
      make_float3(1e-6f, 1e-6f, 1e-6f));

  auto mathNode = [&](ccl::NodeMathType type,
                      ccl::ShaderOutput *v1,
                      ccl::ShaderOutput *v2,
                      float c1 = 0.f,
                      float c2 = 0.f,
                      bool clamp = false) -> ccl::MathNode * {
    auto *n = graph->create_node<ccl::MathNode>();
    n->set_math_type(type);
    n->set_use_clamp(clamp);
    if (v1)
      graph->connect(v1, n->input("Value1"));
    else
      n->set_value1(c1);
    if (v2)
      graph->connect(v2, n->input("Value2"));
    else
      n->set_value2(c2);
    return n;
  };

  // uvw = (P_object - lower) / size, in [0,1] over the field bounds
  auto *texco = graph->create_node<ccl::TextureCoordinateNode>();
  auto *mapping = graph->create_node<ccl::MappingNode>();
  mapping->set_mapping_type(ccl::NODE_MAPPING_TYPE_POINT);
  mapping->set_scale(make_float3(1.f / size.x, 1.f / size.y, 1.f / size.z));
  mapping->set_location(
      make_float3(-lower.x / size.x, -lower.y / size.y, -lower.z / size.z));
  graph->connect(texco->output("Object"), mapping->input("Vector"));

  auto *sep = graph->create_node<ccl::SeparateXYZNode>();
  graph->connect(mapping->output("Vector"), sep->input("Vector"));

  // In-tile (clamped) pixel coordinates: p = 0.5 + clamp01(u) * (n - 1).
  // Keeping samples at least half a texel inside a tile makes the atlas
  // lookup's bilinear filtering clamp-to-edge per Z slice.
  auto inTilePixel = [&](ccl::ShaderOutput *coord, float n) {
    auto *clamped =
        mathNode(ccl::NODE_MATH_ADD, coord, nullptr, 0.f, 0.f, true);
    auto *mad = graph->create_node<ccl::MathNode>();
    mad->set_math_type(ccl::NODE_MATH_MULTIPLY_ADD);
    graph->connect(clamped->output("Value"), mad->input("Value1"));
    mad->set_value2(n - 1.f);
    mad->set_value3(0.5f);
    return mad->output("Value");
  };

  auto *pixelX = inTilePixel(sep->output("X"), nx);
  auto *pixelY = inTilePixel(sep->output("Y"), ny);

  // Continuous slice coordinate: zf = clamp01(w) * (nz - 1)
  auto *wClamped =
      mathNode(ccl::NODE_MATH_ADD, sep->output("Z"), nullptr, 0.f, 0.f, true);
  auto *zf = mathNode(ccl::NODE_MATH_MULTIPLY,
      wClamped->output("Value"),
      nullptr,
      0.f,
      nz - 1.f);

  // Sample one Z slice of the atlas at the in-tile pixel coordinates
  auto sampleSlice = [&](ccl::ShaderOutput *slice) -> ccl::ShaderOutput * {
    auto *tileX = mathNode(
        ccl::NODE_MATH_MODULO, slice, nullptr, 0.f, float(m_tilesX));
    auto *tileYf = mathNode(
        ccl::NODE_MATH_DIVIDE, slice, nullptr, 0.f, float(m_tilesX));
    auto *tileY =
        mathNode(ccl::NODE_MATH_FLOOR, tileYf->output("Value"), nullptr);

    auto atlasUV = [&](ccl::ShaderOutput *tile,
                       ccl::ShaderOutput *pixel,
                       float tileSize,
                       float atlasSize) {
      auto *mad = graph->create_node<ccl::MathNode>();
      mad->set_math_type(ccl::NODE_MATH_MULTIPLY_ADD);
      graph->connect(tile, mad->input("Value1"));
      mad->set_value2(tileSize);
      graph->connect(pixel, mad->input("Value3"));
      auto *uv = mathNode(ccl::NODE_MATH_DIVIDE,
          mad->output("Value"),
          nullptr,
          0.f,
          atlasSize);
      return uv->output("Value");
    };

    auto *u = atlasUV(tileX->output("Value"), pixelX, nx, atlasW);
    auto *v = atlasUV(tileY->output("Value"), pixelY, ny, atlasH);

    auto *uv = graph->create_node<ccl::CombineXYZNode>();
    graph->connect(u, uv->input("X"));
    graph->connect(v, uv->input("Y"));

    auto *tex = graph->create_node<ccl::ImageTextureNode>();
    tex->handle = m_atlas;
    tex->set_colorspace(ccl::u_colorspace_data);
    tex->set_extension(EXTENSION_EXTEND);
    graph->connect(uv->output("Vector"), tex->input("Vector"));

    return tex->output("Color");
  };

  if (!m_linearFilter || m_dims[2] < 2) {
    // Nearest (or single-slice) lookup: slice = floor(zf + 0.5)
    auto *rounded =
        mathNode(ccl::NODE_MATH_ADD, zf->output("Value"), nullptr, 0.f, 0.5f);
    auto *slice =
        mathNode(ccl::NODE_MATH_FLOOR, rounded->output("Value"), nullptr);
    auto *value = mathNode(
        ccl::NODE_MATH_ADD, sampleSlice(slice->output("Value")), nullptr);
    return value->output("Value");
  }

  // Trilinear: mix two bilinear slice lookups.
  // slice = min(floor(zf), nz - 2), t = clamp01(zf - slice)
  auto *sliceFloor =
      mathNode(ccl::NODE_MATH_FLOOR, zf->output("Value"), nullptr);
  auto *slice = mathNode(ccl::NODE_MATH_MINIMUM,
      sliceFloor->output("Value"),
      nullptr,
      0.f,
      nz - 2.f);
  auto *t = mathNode(ccl::NODE_MATH_SUBTRACT,
      zf->output("Value"),
      slice->output("Value"),
      0.f,
      0.f,
      true);
  auto *sliceNext =
      mathNode(ccl::NODE_MATH_ADD, slice->output("Value"), nullptr, 0.f, 1.f);

  auto *value0 = sampleSlice(slice->output("Value"));
  auto *value1 = sampleSlice(sliceNext->output("Value"));

  // value = t * (value1 - value0) + value0
  auto *delta = mathNode(ccl::NODE_MATH_SUBTRACT, value1, value0);
  auto *value = graph->create_node<ccl::MathNode>();
  value->set_math_type(ccl::NODE_MATH_MULTIPLY_ADD);
  graph->connect(t->output("Value"), value->input("Value1"));
  graph->connect(delta->output("Value"), value->input("Value2"));
  graph->connect(value0, value->input("Value3"));

  return value->output("Value");
}

box3 StructuredRegularField::bounds() const
{
  if (!isValid())
    return empty_box3();

  box3 b;
  b.lower[0] = m_origin[0];
  b.lower[1] = m_origin[1];
  b.lower[2] = m_origin[2];

  b.upper[0] = m_origin[0] + (m_dims[0] - 1.f) * m_spacing[0];
  b.upper[1] = m_origin[1] + (m_dims[1] - 1.f) * m_spacing[1];
  b.upper[2] = m_origin[2] + (m_dims[2] - 1.f) * m_spacing[2];

  return b;
}

float StructuredRegularField::stepSize() const
{
  return std::min(
      {std::abs(m_spacing[0]), std::abs(m_spacing[1]), std::abs(m_spacing[2])});
}

bool StructuredRegularField::getDenseVoxelGrid(std::vector<float> &voxels,
    anari_vec::uint3 &dims,
    anari_vec::float3 &origin,
    anari_vec::float3 &spacing) const
{
  // Dimensions come from the data array itself, NOT from m_dims: m_dims is
  // only established by finalize(), and a consumer's finalize can run before
  // this field's within one commit flush (both are priority-0 objects), so a
  // stale m_dims could disagree with a freshly committed 'data' array and
  // over-read it. The consumer is re-finalized after the field either way
  // (change observation), converging on the same result.
  if (!m_data || !voxelToFloatSupported(m_data->elementType()))
    return false;

  dims = m_data->size();
  if (dims[0] == 0 || dims[1] == 0 || dims[2] == 0)
    return false;

  origin = m_origin;
  spacing = m_spacing;

  const size_t n = size_t(dims[0]) * dims[1] * dims[2];
  voxels.resize(n);
  convertVoxelsToFloat(
      m_data->elementType(), m_data->data(), 0, voxels.data(), n);
  return true;
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::SpatialField *);
