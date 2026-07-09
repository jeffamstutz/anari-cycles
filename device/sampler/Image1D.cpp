// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Image1D.h"
#include "SamplerUtils.h"
#include "cycles_math.h"

namespace anari_cycles {

Image1D::Image1D(CyclesGlobalState *d) : Sampler(d), m_image(this) {}

bool Image1D::isValid() const
{
  return m_image;
}

void Image1D::commitParameters()
{
  Sampler::commitParameters();
  m_image = getParamObject<Array1D>("image");
  m_linearFilter = getParamString("filter", "linear") != "nearest";
  // the registry names this 'wrapMode'; accept the common 'wrapMode1' too
  m_wrapMode = helium::wrapModeFromString(
      getParamString("wrapMode", getParamString("wrapMode1", "clampToEdge")));
}

void Image1D::finalize()
{
  if (isValid()) {
    auto &state = *deviceState();
    auto loader = std::make_unique<SamplerImageLoader>(m_image.get());
    ccl::ImageParams params;
    params.alpha_type = IMAGE_ALPHA_AUTO;
    params.colorspace = imageColorspace(m_image->elementType());
    params.extension = cyclesExtension(m_wrapMode);
    params.interpolation =
        m_linearFilter ? INTERPOLATION_LINEAR : INTERPOLATION_CLOSEST;
    m_handle =
        state.scene->image_manager->add_image(std::move(loader), params, false);
  }
  // notify observing materials so they rebuild their graphs on the new handle
  Object::finalize();
}

Sampler::SamplerOutputs Image1D::createNodeGraph(ccl::ShaderGraph *graph)
{
  if (!graph || m_handle.empty())
    return {};

  auto in = makeAttributeInput(graph, m_inAttribute);
  auto uv = applyAffineTransform(graph, in, m_inTransform, m_inOffset);

  auto *tex = graph->create_node<ccl::ImageTextureNode>();
  tex->handle = m_handle;
  tex->set_colorspace(ccl::u_colorspace_auto);
  tex->set_extension(cyclesExtension(m_wrapMode));
  tex->set_interpolation(
      m_linearFilter ? INTERPOLATION_LINEAR : INTERPOLATION_CLOSEST);
  graph->connect(uv.color, tex->input("Vector"));

  ColorAlpha sampled{tex->output("Color"), tex->output("Alpha")};
  auto out = applyAffineTransform(graph, sampled, m_outTransform, m_outOffset);
  return makeStandardOutputs(graph, out);
}

} // namespace anari_cycles
