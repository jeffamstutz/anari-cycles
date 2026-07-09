// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

// std
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
// ours
#include "SpatialField.h"
#include "VolumeImageLoader.h"
// cycles
#include "scene/geometry.h"
#include "scene/scene.h"
#include "scene/shader_nodes.h"

#if defined(WITH_OPENVDB) && defined(WITH_NANOVDB)
// Cycles' native VDB volume path: VDBImageLoader converts an OpenVDB grid to
// the NanoVDB image sampled by the kernel, so both libraries are required.
#define ANARI_CYCLES_HAS_VDB 1
// cycles
#include "scene/image_vdb.h" // pulls in <nanovdb/NanoVDB.h> + GridHandle
// openvdb/nanovdb
#include <openvdb/openvdb.h>
#if NANOVDB_MAJOR_VERSION_NUMBER > 32 || \
    (NANOVDB_MAJOR_VERSION_NUMBER == 32 && NANOVDB_MINOR_VERSION_NUMBER >= 7)
#include <nanovdb/tools/NanoToOpenVDB.h>
#else
#include <nanovdb/util/NanoToOpenVDB.h>
#endif
#endif

namespace anari_cycles {

namespace {

// All spatial field images are non-color voxel data; only the loader and the
// interpolation mode differ between the atlas and VDB paths.
ccl::ImageHandle addFieldImage(CyclesGlobalState &state,
    std::unique_ptr<ccl::ImageLoader> loader,
    InterpolationType interpolation)
{
  ccl::ImageParams params;
  params.alpha_type = IMAGE_ALPHA_AUTO;
  params.colorspace = ccl::u_colorspace_data;
  params.interpolation = interpolation;
  return state.scene->image_manager->add_image(
      std::move(loader), params, false);
}

} // namespace

#ifdef ANARI_CYCLES_HAS_VDB

namespace {

// VDBImageLoader with device-controlled precision and no value clipping:
// ANARI fields hand us explicit voxel data, so nothing may be dropped or
// quantized beyond what the input grid already encodes.
class FieldVDBImageLoader : public ccl::VDBImageLoader
{
 public:
  FieldVDBImageLoader(
      openvdb::GridBase::ConstPtr g, const char *name, int gridPrecision)
      : VDBImageLoader(std::move(g), name, 0.f)
  {
    precision = gridPrecision;
  }

  // Dense scalar voxels (structuredRegular filter="cubic"): 'objectToTexture'
  // maps object space to the [0,1]^3 texture space spanning dims*spacing from
  // the field origin, which lands voxel centers on integer grid indices.
  FieldVDBImageLoader(const float *voxels,
      const anari_vec::uint3 &dims,
      const ccl::Transform &objectToTexture,
      const char *name)
      : VDBImageLoader(name, 0.f)
  {
    precision = 32;
    grid_from_dense_voxels(dims[0], dims[1], dims[2], 1, voxels, objectToTexture);
  }
};

openvdb::GridBase::Ptr nanovdbGridToOpenVDB(const nanovdb::GridHandle<> &handle)
{
#if NANOVDB_MAJOR_VERSION_NUMBER > 32 || \
    (NANOVDB_MAJOR_VERSION_NUMBER == 32 && NANOVDB_MINOR_VERSION_NUMBER >= 7)
  return nanovdb::tools::nanoToOpenVDB(handle);
#else
  return nanovdb::nanoToOpenVDB(handle);
#endif
}

// Precision VDBImageLoader re-encodes the grid with, chosen to not degrade
// the input: quantized NanoVDB grids stay quantized, everything else keeps
// full float precision.
int gridImagePrecision(nanovdb::GridType gridType)
{
  switch (gridType) {
    case nanovdb::GridType::Fp4:
    case nanovdb::GridType::Fp8:
    case nanovdb::GridType::Fp16:
    case nanovdb::GridType::Half:
      return 16;
    case nanovdb::GridType::FpN:
      return 0;
    default:
      return 32;
  }
}

} // namespace

#endif // ANARI_CYCLES_HAS_VDB

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

bool SpatialField::cubicVolumeInterpolation() const
{
  return false;
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

  const std::string filter = getParamString("filter", "linear");
  if (filter == "nearest")
    m_filter = Filter::NEAREST;
  else if (filter == "cubic")
    m_filter = Filter::CUBIC;
  else
    m_filter = Filter::LINEAR;
}

void StructuredRegularField::finalize()
{
  m_atlas = ccl::ImageHandle();
  m_voxelImage = ccl::ImageHandle();

  if (!m_data) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'data' on 'structuredRegular' field");
    SpatialField::finalize();
    return;
  }

  m_dims = m_data->size();
  if (!isValid()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'data' on 'structuredRegular' field has a zero-sized dimension");
    SpatialField::finalize();
    return;
  }

  if (m_filter == Filter::CUBIC && finalizeCubicGrid()) {
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

  m_atlas = addFieldImage(*deviceState(),
      std::make_unique<VolumeImageLoader>(m_data.get(), m_tilesX, m_tilesY),
      m_filter == Filter::NEAREST ? INTERPOLATION_CLOSEST
                                  : INTERPOLATION_LINEAR);

  SpatialField::finalize();
}

bool StructuredRegularField::finalizeCubicGrid()
{
#ifdef ANARI_CYCLES_HAS_VDB
  if (!voxelToFloatSupported(m_data->elementType())) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "unsupported voxel type on 'structuredRegular' field with "
        "filter='cubic'; falling back to 'linear'");
    return false;
  }

  const size_t n = size_t(m_dims[0]) * m_dims[1] * m_dims[2];
  std::vector<float> voxels(n);
  convertVoxelsToFloat(
      m_data->elementType(), m_data->data(), 0, voxels.data(), n);

  openvdb::initialize();

  const ccl::Transform objectToTexture =
      transform_scale(make_float3(1.f / (m_dims[0] * m_spacing[0]),
          1.f / (m_dims[1] * m_spacing[1]),
          1.f / (m_dims[2] * m_spacing[2]))) *
      transform_translate(
          make_float3(-m_origin[0], -m_origin[1], -m_origin[2]));

  try {
    m_voxelImage = addFieldImage(*deviceState(),
        std::make_unique<FieldVDBImageLoader>(voxels.data(),
            m_dims,
            objectToTexture,
            "ANARI structuredRegular cubic"),
        INTERPOLATION_LINEAR); // the kernel forces cubic via the shader flag
  } catch (const std::exception &e) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "building the VDB grid for filter='cubic' on 'structuredRegular' "
        "field failed (%s); falling back to 'linear'",
        e.what());
    return false;
  }
  return true;
#else
  reportMessage(ANARI_SEVERITY_WARNING,
      "filter='cubic' on 'structuredRegular' field requires a device built "
      "with WITH_CYCLES_NANOVDB=ON and WITH_CYCLES_OPENVDB=ON; falling back "
      "to 'linear'");
  return false;
#endif
}

bool StructuredRegularField::isValid() const
{
  return m_data && m_dims[0] > 0 && m_dims[1] > 0 && m_dims[2] > 0;
}

bool StructuredRegularField::cubicVolumeInterpolation() const
{
  return !m_voxelImage.empty();
}

ccl::ShaderOutput *StructuredRegularField::createCyclesSamplingNodes(
    ccl::ShaderGraph *graph)
{
  if (!isValid())
    return nullptr;
  if (!m_voxelImage.empty())
    return createVoxelSamplingNodes(graph);
  return createAtlasSamplingNodes(graph);
}

ccl::ShaderOutput *StructuredRegularField::createAtlasSamplingNodes(
    ccl::ShaderGraph *graph)
{
  if (m_atlas.empty())
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

  if (m_filter == Filter::NEAREST || m_dims[2] < 2) {
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

// NanoVDBField //

NanoVDBField::NanoVDBField(CyclesGlobalState *s)
    : SpatialField(s), m_data(this), m_bounds(empty_box3())
{}

NanoVDBField::~NanoVDBField() = default;

void NanoVDBField::commitParameters()
{
  m_data = getParamObject<Array1D>("data");
  m_linearFilter = getParamString("filter", "linear") != "nearest";
}

void NanoVDBField::finalize()
{
  m_voxelImage = ccl::ImageHandle();
  m_bounds = empty_box3();
  m_stepSize = 0.f;

  if (!m_data) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'data' on 'nanovdb' field");
    SpatialField::finalize();
    return;
  }

  if (m_data->elementType() != ANARI_UINT8) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'data' on 'nanovdb' field must be an ANARI_UINT8 array "
        "(a serialized NanoVDB grid)");
    SpatialField::finalize();
    return;
  }

#ifndef ANARI_CYCLES_HAS_VDB
  reportMessage(ANARI_SEVERITY_WARNING,
      "'nanovdb' field requires a device built with WITH_CYCLES_NANOVDB=ON "
      "and WITH_CYCLES_OPENVDB=ON");
#else
  openvdb::initialize();

  try {
    // Copy the blob into a NanoVDB-owned buffer: ANARI arrays guarantee no
    // particular alignment while NanoVDB grids require
    // NANOVDB_DATA_ALIGNMENT, and the copy also decouples the Cycles image
    // (loaded lazily at render time) from the ANARI array's lifetime.
    auto buffer = nanovdb::HostBuffer::create(m_data->size());
    std::memcpy(buffer.data(), m_data->data(), m_data->size());
    const nanovdb::GridHandle<> handle(std::move(buffer));

    const nanovdb::GridMetaData *metadata = handle.gridMetaData(0);
    if (!metadata || !metadata->isValid()) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'data' on 'nanovdb' field is not a valid serialized NanoVDB grid");
      SpatialField::finalize();
      return;
    }
    if (handle.gridCount() > 1) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'data' on 'nanovdb' field holds %u grids; using the first",
          handle.gridCount());
    }
    if (metadata->isEmpty()) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'data' on 'nanovdb' field has no active voxels");
      SpatialField::finalize();
      return;
    }

    if (metadata->hasBBox()) {
      const auto &worldBounds = metadata->worldBBox();
      m_bounds.lower[0] = float(worldBounds.min()[0]);
      m_bounds.lower[1] = float(worldBounds.min()[1]);
      m_bounds.lower[2] = float(worldBounds.min()[2]);
      m_bounds.upper[0] = float(worldBounds.max()[0]);
      m_bounds.upper[1] = float(worldBounds.max()[1]);
      m_bounds.upper[2] = float(worldBounds.max()[2]);
    } else {
      // Grids serialized without stats lack the precomputed world-space
      // bounds; derive them by mapping the index-space bounds (expanded to
      // voxel corners) through the grid transform.
      const auto &indexBounds = metadata->indexBBox();
      const double lo[3] = {double(indexBounds.min()[0]),
          double(indexBounds.min()[1]),
          double(indexBounds.min()[2])};
      const double hi[3] = {double(indexBounds.max()[0]) + 1.0,
          double(indexBounds.max()[1]) + 1.0,
          double(indexBounds.max()[2]) + 1.0};
      for (int corner = 0; corner < 8; ++corner) {
        const nanovdb::Vec3d p = metadata->map().applyMap(
            nanovdb::Vec3d(corner & 1 ? hi[0] : lo[0],
                corner & 2 ? hi[1] : lo[1],
                corner & 4 ? hi[2] : lo[2]));
        for (int axis = 0; axis < 3; ++axis) {
          m_bounds.lower[axis] = std::min(m_bounds.lower[axis], float(p[axis]));
          m_bounds.upper[axis] = std::max(m_bounds.upper[axis], float(p[axis]));
        }
      }
    }

    const auto voxelSize = metadata->voxelSize();
    m_stepSize = float(std::min({voxelSize[0], voxelSize[1], voxelSize[2]}));

    // Cycles samples VDB volumes from a NanoVDB image built out of an OpenVDB
    // grid (VDBImageLoader), so round-trip the input grid through OpenVDB.
    openvdb::GridBase::Ptr grid = nanovdbGridToOpenVDB(handle);
    if (!grid) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "unsupported NanoVDB grid type on 'nanovdb' field");
      SpatialField::finalize();
      return;
    }

    m_voxelImage = addFieldImage(*deviceState(),
        std::make_unique<FieldVDBImageLoader>(
            grid, "ANARI nanovdb", gridImagePrecision(metadata->gridType())),
        m_linearFilter ? INTERPOLATION_LINEAR : INTERPOLATION_CLOSEST);
  } catch (const std::exception &e) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'data' on 'nanovdb' field is not a valid serialized NanoVDB grid: %s",
        e.what());
  }
#endif

  SpatialField::finalize();
}

ccl::ShaderOutput *NanoVDBField::createCyclesSamplingNodes(
    ccl::ShaderGraph *graph)
{
  return createVoxelSamplingNodes(graph);
}

box3 NanoVDBField::bounds() const
{
  return m_bounds;
}

float NanoVDBField::stepSize() const
{
  return m_stepSize;
}

bool NanoVDBField::isValid() const
{
  return m_data && !m_voxelImage.empty();
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::SpatialField *);
