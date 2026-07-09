// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "TransferFunction1D.h"
// std
#include <algorithm>
#include <vector>
// cycles
#include "scene/mesh.h"
#include "scene/scene.h"
#include "scene/shader.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"

namespace anari_cycles {

namespace {

// Piecewise-linear resample of a transfer function array to 'size' entries.
template <typename T>
T sampleArray(const std::vector<T> &values, float x)
{
  if (values.size() == 1)
    return values[0];
  const float f = ccl::clamp(x, 0.f, 1.f) * float(values.size() - 1);
  const size_t i0 = size_t(f);
  const size_t i1 = std::min(i0 + 1, values.size() - 1);
  const float t = f - float(i0);
  return values[i0] * (1.f - t) + values[i1] * t;
}

} // namespace

TransferFunction1D::TransferFunction1D(CyclesGlobalState *s)
    : FieldVolume(s, "ANARI TransferFunction1D"),
      m_field(this),
      m_colorData(this),
      m_opacityData(this)
{}

TransferFunction1D::~TransferFunction1D() = default;

bool TransferFunction1D::isValid() const
{
  return m_field && m_field->isValid() && m_colorData && m_opacityData;
}

void TransferFunction1D::commitParameters()
{
  m_field = getParamObject<SpatialField>("value");
  m_valueRange = getParam<helium::box1>("valueRange", helium::box1{0.f, 1.f});
  m_colorData = getParamObject<Array1D>("color");
  m_opacityData = getParamObject<Array1D>("opacity");
  m_unitDistance = getParam<float>("unitDistance", 1.f);
  m_id = getParam<uint32_t>("id", ~0u);

  if (!m_field) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "no spatial field provided to transferFunction1D volume");
  }
  if (!m_colorData) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "no color data provided to transferFunction1D volume");
  }
  if (!m_opacityData) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "no opacity data provided to transferFunction1D volume");
  }
}

void TransferFunction1D::finalize()
{
  if (!isValid()) {
    retireMesh();
    Volume::finalize();
    return;
  }

  m_bounds = m_field->bounds();

  rebuildCyclesShaderGraph();
  syncCyclesMesh({m_field.get()});

  Volume::finalize();
}

void TransferFunction1D::rebuildCyclesShaderGraph()
{
  auto &state = *deviceState();

  auto graph = std::make_unique<ccl::ShaderGraph>();

  auto *fieldValue = m_field->createCyclesSamplingNodes(graph.get());
  if (fieldValue) {
    auto *mapRange = graph->create_node<ccl::MapRangeNode>();
    mapRange->set_clamp(true);
    mapRange->set_from_min(m_valueRange.lower);
    mapRange->set_from_max(m_valueRange.upper);
    graph->connect(fieldValue, mapRange->input("Value"));

    std::vector<ccl::float3> colors;
    if (m_colorData->elementType() == ANARI_FLOAT32_VEC3) {
      auto *c = m_colorData->beginAs<anari_vec::float3>();
      for (size_t i = 0; i < m_colorData->size(); ++i)
        colors.push_back(ccl::make_float3(c[i][0], c[i][1], c[i][2]));
    } else if (m_colorData->elementType() == ANARI_FLOAT32_VEC4) {
      auto *c = m_colorData->beginAs<anari_vec::float4>();
      for (size_t i = 0; i < m_colorData->size(); ++i)
        colors.push_back(ccl::make_float3(c[i][0], c[i][1], c[i][2]));
    } else {
      reportMessage(ANARI_SEVERITY_WARNING,
          "unsupported color array element type on transferFunction1D volume");
      colors.push_back(ccl::make_float3(1.f, 1.f, 1.f));
    }

    std::vector<float> opacities;
    if (m_opacityData->elementType() == ANARI_FLOAT32) {
      auto *o = m_opacityData->beginAs<float>();
      opacities.assign(o, o + m_opacityData->size());
    } else {
      reportMessage(ANARI_SEVERITY_WARNING,
          "unsupported opacity array element type on transferFunction1D volume");
      opacities.push_back(1.f);
    }

    // Guard against zero-length arrays (sampleArray would read out of bounds)
    if (colors.empty())
      colors.push_back(ccl::make_float3(1.f, 1.f, 1.f));
    if (opacities.empty())
      opacities.push_back(1.f);

    // Resample color/opacity onto a single shared LUT: Cycles' RGBRampNode
    // silently compiles to nothing when ramp/alpha sizes differ, and the ANARI
    // arrays may have different lengths. Size the LUT to preserve the finer of
    // the two inputs (the ramp itself interpolates between entries).
    const int lutSize = int(std::min<size_t>(
        std::max({colors.size(), opacities.size(), size_t(2)}), 4096));

    auto *ramp = graph->create_node<ccl::RGBRampNode>();
    ramp->set_interpolate(true);
    ramp->get_ramp().resize(lutSize);
    ramp->get_ramp_alpha().resize(lutSize);
    for (int i = 0; i < lutSize; ++i) {
      const float x = float(i) / float(lutSize - 1);
      ramp->get_ramp()[i] = sampleArray(colors, x);
      ramp->get_ramp_alpha()[i] = sampleArray(opacities, x);
    }
    graph->connect(mapRange->output("Result"), ramp->input("Fac"));

    // sigma_t = opacity / unitDistance
    auto *density = graph->create_node<ccl::MathNode>();
    density->set_math_type(ccl::NODE_MATH_DIVIDE);
    density->set_value2(m_unitDistance > 0.f ? m_unitDistance : 1.f);
    graph->connect(ramp->output("Alpha"), density->input("Value1"));

    auto *volumeNode = graph->create_node<ccl::PrincipledVolumeNode>();
    volumeNode->set_density_attribute(ustring());
    volumeNode->set_color_attribute(ustring());
    volumeNode->set_temperature_attribute(ustring());
    volumeNode->set_blackbody_intensity(0.f);
    // Emission + absorption (no scattering albedo): this matches the alpha
    // compositing model reference devices use for transferFunction1D, where
    // sample color is accumulated proportional to opacity.
    volumeNode->set_color(ccl::zero_float3());
    graph->connect(density->output("Value"), volumeNode->input("Density"));
    graph->connect(ramp->output("Color"), volumeNode->input("Emission Color"));
    graph->connect(
        density->output("Value"), volumeNode->input("Emission Strength"));

    graph->connect(
        volumeNode->output("Volume"), graph->output()->input("Volume"));
  } else {
    reportMessage(ANARI_SEVERITY_WARNING,
        "transferFunction1D volume could not create field sampling nodes");
  }

  m_shader->set_graph(std::move(graph));
  applyVolumeStepRate(m_field.get());
  m_shader->tag_update(state.scene);
}

} // namespace anari_cycles
