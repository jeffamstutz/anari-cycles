// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Image2D.h"
#include "SamplerUtils.h"
#include "cycles_math.h"

namespace anari_cycles {

Image2D::Image2D(CyclesGlobalState *d) : Sampler(d) {}

bool Image2D::isValid() const
{
  return m_image;
}

void Image2D::commitParameters()
{
  Sampler::commitParameters();
  m_image = getParamObject<Array2D>("image");
  m_linearFilter = getParamString("filter", "linear") != "nearest";
  m_wrapMode1 =
      helium::wrapModeFromString(getParamString("wrapMode1", "clampToEdge"));
  m_wrapMode2 =
      helium::wrapModeFromString(getParamString("wrapMode2", "clampToEdge"));
}

void Image2D::finalize()
{
  if (isValid()) {
    auto &state = *deviceState();
    auto loader = std::make_unique<SamplerImageLoader>(m_image.ptr);
    ccl::ImageParams params;
    params.alpha_type = IMAGE_ALPHA_AUTO;
    params.colorspace = imageColorspace(m_image->elementType());
    params.extension = baseExtension();
    params.interpolation =
        m_linearFilter ? INTERPOLATION_LINEAR : INTERPOLATION_CLOSEST;
    m_handle =
        state.scene->image_manager->add_image(std::move(loader), params, false);
  }
  // notify observing materials so they rebuild their graphs on the new handle
  Object::finalize();
}

Sampler::SamplerOutputs Image2D::createNodeGraph(ccl::ShaderGraph *graph)
{
  if (!graph || m_handle.empty())
    return {};

  auto in = makeAttributeInput(graph, m_inAttribute);
  auto uv = applyAffineTransform(graph, in, m_inTransform, m_inOffset);

  ccl::ShaderOutput *coords = uv.color;
  if (m_wrapMode1 != m_wrapMode2) {
    auto *separate = graph->create_node<ccl::SeparateXYZNode>();
    graph->connect(coords, separate->input("Vector"));
    auto *combine = graph->create_node<ccl::CombineXYZNode>();
    graph->connect(wrapAxis(graph, separate->output("X"), m_wrapMode1,
                       m_image->size(0)),
        combine->input("X"));
    graph->connect(wrapAxis(graph, separate->output("Y"), m_wrapMode2,
                       m_image->size(1)),
        combine->input("Y"));
    coords = combine->output("Vector");
  }

  auto *tex = graph->create_node<ccl::ImageTextureNode>();
  tex->handle = m_handle;
  tex->set_colorspace(ccl::u_colorspace_auto);
  tex->set_extension(baseExtension());
  tex->set_interpolation(
      m_linearFilter ? INTERPOLATION_LINEAR : INTERPOLATION_CLOSEST);
  graph->connect(coords, tex->input("Vector"));

  ColorAlpha sampled{tex->output("Color"), tex->output("Alpha")};
  auto out = applyAffineTransform(graph, sampled, m_outTransform, m_outOffset);
  return makeStandardOutputs(graph, out);
}

} // namespace anari_cycles
