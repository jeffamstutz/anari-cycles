// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Volume.h"
// std
#include <numeric>
// cycles
#include "graph/node_xml.h"
#include "scene/shader_nodes.h"
#include "scene/volume.h"
#include "util/path.h"

namespace anari_cycles {

Volume::Volume(CyclesGlobalState *s) : Object(ANARI_VOLUME, s) {}

Volume::~Volume() = default;

Volume *Volume::createInstance(std::string_view subtype, CyclesGlobalState *s)
{
  if (subtype == "transferFunction1D")
    return new TransferFunction1D(s);
  else
    return (Volume *)new UnknownObject(ANARI_VOLUME, subtype, s);
}

// Subtypes ///////////////////////////////////////////////////////////////////

TransferFunction1D::TransferFunction1D(CyclesGlobalState *s) : Volume(s)
{
  auto &state = *deviceState();

  auto shader = std::make_unique<ccl::Shader>();
  m_shader = shader.get();
  state.scene->shaders.push_back(std::move(shader));

  auto graph = std::make_unique<ccl::ShaderGraph>();
  m_graph = graph.get();

  m_volumeNode = m_graph->create_node<ccl::PrincipledVolumeNode>();
  m_volumeNode->set_density_attribute(ustring("never-connected"));

  m_attributeNode = m_graph->create_node<ccl::AttributeNode>();
  m_attributeNode->set_attribute(ustring("voxels"));

  m_mapRangeNode = m_graph->create_node<ccl::MapRangeNode>();
  m_mapRangeNode->set_clamp(true);

  m_rgbRampNode = m_graph->create_node<ccl::RGBRampNode>();

  m_graph->connect(
      m_volumeNode->output("Volume"), m_graph->output()->input("Volume"));
  m_graph->connect(
      m_attributeNode->output("Fac"), m_mapRangeNode->input("Value"));
  m_graph->connect(
      m_mapRangeNode->output("Result"), m_rgbRampNode->input("Fac"));
  m_graph->connect(
      m_rgbRampNode->output("Color"), m_volumeNode->input("Color"));
  m_graph->connect(
      m_rgbRampNode->output("Alpha"), m_volumeNode->input("Density"));

  m_shader->set_graph(std::move(graph));
  m_shader->tag_update(state.scene);
}

TransferFunction1D::~TransferFunction1D()
{
  auto &state = *deviceState();
  state.scene->shaders.erase(cyclesShader());
}

ccl::Shader *TransferFunction1D::cyclesShader()
{
  return m_shader;
}

bool TransferFunction1D::isValid() const
{
  return m_field && m_field->isValid() && m_colorData && m_opacityData;
}

void TransferFunction1D::commitParameters()
{
  Volume::commitParameters();

  m_field = getParamObject<SpatialField>("value");
  if (!m_field) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "no spatial field provided to transferFunction1D volume");
    return;
  }

  m_bounds = m_field->bounds();

  m_valueRange = getParam<helium::box1>("valueRange", helium::box1{0.f, 1.f});

  m_colorData = getParamObject<helium::Array1D>("color");
  m_opacityData = getParamObject<helium::Array1D>("opacity");
  m_densityScale = getParam<float>("unitDistance", 1.f);

  if (!m_colorData) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "no color data provided to transferFunction1D volume");
    return;
  }

  if (!m_opacityData) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "no opacity data provided to transfer function");
    return;
  }

  if (m_mapRangeNode != nullptr) {
    m_mapRangeNode->set_from_min(m_valueRange.lower);
    m_mapRangeNode->set_from_max(m_valueRange.upper);
  }

  if (m_mathNode != nullptr) {
    m_mathNode->set_value2(m_densityScale);
  }

  // m_colorData, m_opacityData
  auto *colorData = m_colorData->beginAs<anari_vec::float3>();
  auto *opacityData = m_opacityData->beginAs<float>();

  if (m_rgbRampNode != nullptr) {
    m_rgbRampNode->get_ramp().resize(m_colorData->size());
    m_rgbRampNode->get_ramp_alpha().resize(m_opacityData->size());

    for (size_t i = 0; i < m_colorData->size(); ++i) {
      m_rgbRampNode->get_ramp()[i] =
          (ccl::make_float3(colorData[i][0], colorData[i][1], colorData[i][2]));
    }

    for (size_t i = 0; i < m_opacityData->size(); ++i) {
      m_rgbRampNode->get_ramp_alpha()[i] = opacityData[i];
    }
  }

  m_shader->tag_update(deviceState()->scene);
}

std::unique_ptr<ccl::Geometry> TransferFunction1D::makeCyclesGeometry()
{
  auto g = m_field->makeCyclesGeometry();
  ccl::array<ccl::Node *> used_shaders;
  used_shaders.push_back_slow(cyclesShader());
  g->set_used_shaders(used_shaders);
  return g;
}

box3 TransferFunction1D::bounds() const
{
  return m_bounds;
}

// void TransferFunction1D::cleanup() {}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Volume *);
