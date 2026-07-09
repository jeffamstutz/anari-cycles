// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "PrincipledVolume.h"
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

PrincipledVolume::PrincipledVolume(CyclesGlobalState *s)
    : FieldVolume(s, "ANARI PrincipledVolume"),
      m_field(this),
      m_temperatureField(this)
{}

PrincipledVolume::~PrincipledVolume() = default;

bool PrincipledVolume::isValid() const
{
  return m_field && m_field->isValid();
}

void PrincipledVolume::commitParameters()
{
  m_field = getParamObject<SpatialField>("value");
  m_densityScale = getParam<float>("densityScale", 1.f);
  m_color = getParam<float3>("color", make_float3(0.5f, 0.5f, 0.5f));
  m_anisotropy = getParam<float>("anisotropy", 0.f);
  m_absorptionColor = getParam<float3>("absorptionColor", zero_float3());
  m_emissionStrength = getParam<float>("emissionStrength", 0.f);
  m_emissionColor =
      getParam<float3>("emissionColor", make_float3(1.f, 1.f, 1.f));
  m_blackbodyIntensity = getParam<float>("blackbodyIntensity", 0.f);
  m_blackbodyTint =
      getParam<float3>("blackbodyTint", make_float3(1.f, 1.f, 1.f));
  m_temperatureField = getParamObject<SpatialField>("temperature");
  m_temperature = getParam<float>("temperature", 1000.f);
  m_id = getParam<uint32_t>("id", ~0u);

  if (!m_field) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "no spatial field provided to principled volume ('value' parameter)");
  }
}

void PrincipledVolume::finalize()
{
  if (!isValid()) {
    retireMesh();
    Volume::finalize();
    return;
  }

  m_bounds = m_field->bounds();

  rebuildCyclesShaderGraph();
  syncCyclesMesh({m_field.get(), m_temperatureField.get()});

  Volume::finalize();
}

void PrincipledVolume::rebuildCyclesShaderGraph()
{
  auto &state = *deviceState();

  auto graph = std::make_unique<ccl::ShaderGraph>();

  auto *fieldValue = m_field->createCyclesSamplingNodes(graph.get());
  if (fieldValue) {
    auto *volumeNode = graph->create_node<ccl::PrincipledVolumeNode>();
    // The density/color/temperature sockets are driven by links (or
    // constants), not by named voxel grid attributes.
    volumeNode->set_density_attribute(ustring());
    volumeNode->set_color_attribute(ustring());
    volumeNode->set_temperature_attribute(ustring());

    volumeNode->set_color(m_color);
    volumeNode->set_anisotropy(m_anisotropy);
    volumeNode->set_absorption_color(m_absorptionColor);
    volumeNode->set_emission_strength(m_emissionStrength);
    volumeNode->set_emission_color(m_emissionColor);
    volumeNode->set_blackbody_intensity(m_blackbodyIntensity);
    volumeNode->set_blackbody_tint(m_blackbodyTint);

    // density = fieldValue * densityScale
    if (m_densityScale != 1.f) {
      auto *scale = graph->create_node<ccl::MathNode>();
      scale->set_math_type(ccl::NODE_MATH_MULTIPLY);
      scale->set_value2(m_densityScale);
      graph->connect(fieldValue, scale->input("Value1"));
      graph->connect(scale->output("Value"), volumeNode->input("Density"));
    } else {
      graph->connect(fieldValue, volumeNode->input("Density"));
    }

    // Blackbody temperature (K): either a second spatial field (fire) or a
    // constant. An invalid field falls back to the constant with a warning.
    if (m_temperatureField && m_temperatureField->isValid()) {
      auto *temperatureValue =
          m_temperatureField->createCyclesSamplingNodes(graph.get());
      if (temperatureValue)
        graph->connect(temperatureValue, volumeNode->input("Temperature"));
      else
        volumeNode->set_temperature(m_temperature);
    } else {
      if (m_temperatureField) {
        reportMessage(ANARI_SEVERITY_WARNING,
            "invalid 'temperature' spatial field on principled volume; "
            "using the constant 'temperature' value instead");
      }
      volumeNode->set_temperature(m_temperature);
    }

    graph->connect(
        volumeNode->output("Volume"), graph->output()->input("Volume"));
  } else {
    reportMessage(ANARI_SEVERITY_WARNING,
        "principled volume could not create field sampling nodes");
  }

  m_shader->set_graph(std::move(graph));
  applyVolumeStepRate(m_field.get());
  m_shader->tag_update(state.scene);
}

} // namespace anari_cycles
