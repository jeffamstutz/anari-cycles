// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Renderer.h"
// helium
#include "helium/helium_math.h"
// cycles
#include "scene/background.h"
#include "scene/integrator.h"
#include "scene/pass.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
// std
#include <algorithm>

namespace anari_cycles {

namespace {

// Element types accepted for the 'background' image
// (KHR_RENDERER_BACKGROUND_IMAGE registry list).
bool backgroundImageTypeSupported(anari::DataType type)
{
  switch (type) {
  case ANARI_UFIXED8:
  case ANARI_UFIXED8_VEC2:
  case ANARI_UFIXED8_VEC3:
  case ANARI_UFIXED8_VEC4:
  case ANARI_UFIXED8_R_SRGB:
  case ANARI_UFIXED8_RA_SRGB:
  case ANARI_UFIXED8_RGB_SRGB:
  case ANARI_UFIXED8_RGBA_SRGB:
  case ANARI_UFIXED16:
  case ANARI_UFIXED16_VEC2:
  case ANARI_UFIXED16_VEC3:
  case ANARI_UFIXED16_VEC4:
  case ANARI_UFIXED32:
  case ANARI_UFIXED32_VEC2:
  case ANARI_UFIXED32_VEC3:
  case ANARI_UFIXED32_VEC4:
  case ANARI_FLOAT32:
  case ANARI_FLOAT32_VEC2:
  case ANARI_FLOAT32_VEC3:
  case ANARI_FLOAT32_VEC4:
    return true;
  default:
    return false;
  }
}

// Convert one texel to linear float RGBA with the ANARI attribute conversion
// rules. helium::readAsAttributeValueFlat() covers every accepted element
// type, but it tone-maps all components of the sRGB formats while the sRGB
// transfer only applies to color channels -- alpha is always linear
// (matching SamplerImageLoader/Cycles). Fix up the two sRGB formats that
// carry alpha; RA pairs additionally expand as (r, 0, 0, a) rather than as a
// generic 2-component color.
math::float4 texelToLinearRGBA(
    const void *data, anari::DataType type, uint64_t i)
{
  if (type == ANARI_UFIXED8_RA_SRGB) {
    const auto *t = static_cast<const uint8_t *>(data) + 2 * i;
    return {helium::toneMap<helium::ToneMapMode::FROM_SRGB>(t[0] / 255.f),
        0.f,
        0.f,
        t[1] / 255.f};
  }
  auto v = helium::readAsAttributeValueFlat(data, type, i);
  if (type == ANARI_UFIXED8_RGBA_SRGB)
    v.w = (static_cast<const uint8_t *>(data) + 4 * i)[3] / 255.f;
  return v;
}

} // namespace

Renderer::Renderer(CyclesGlobalState *s) : Object(ANARI_RENDERER, s)
{
  commitParameters();
}

Renderer::~Renderer() = default;

void Renderer::commitParameters()
{
  auto backgroundColor =
      getParam<math::float4>("background", {0.f, 0.f, 0.f, 1.f});
  m_needsUpdateStatus.background |= (m_backgroundColor != backgroundColor);
  m_backgroundColor = backgroundColor;

  // KHR_RENDERER_BACKGROUND_IMAGE: 'background' set as an ARRAY2D replaces
  // the color and vice versa (an ANARI parameter holds one typed value, so
  // getParamObject() is null whenever the color form is set).
  Array2D *backgroundImage = getParamObject<Array2D>("background");
  m_needsUpdateStatus.background |=
      (m_backgroundImageArray.ptr != backgroundImage);
  m_needsUpdateStatus.backgroundImage |=
      (m_backgroundImageArray.ptr != backgroundImage);
  m_backgroundImageArray = backgroundImage;

  auto ambientColor = getParam<math::float3>("ambientColor", {1.f, 1.f, 1.f});
  m_needsUpdateStatus.ambientLight |= (m_ambientColor != ambientColor);
  m_ambientColor = ambientColor;
  auto ambientRadiance = getParam<float>("ambientRadiance", 1.f);
  m_needsUpdateStatus.ambientLight |= (m_ambientRadiance != ambientRadiance);
  m_ambientRadiance = ambientRadiance;

  m_runAsync = getParam<bool>("runAsync", true);

  m_pixelSamples = std::max(1, getParam<int>("pixelSamples", 1));

  auto denoise = getParam<bool>("denoise", false);
  m_needsUpdateStatus.denoise |= (m_denoise != denoise);
  m_denoise = denoise;
}

void Renderer::rebuildDefaultBackgroundShader()
{
  // Setup the background shader. Camera rays see the 'background' color,
  // while all other rays see 'ambientColor * ambientRadiance' — a uniform
  // dome implementing KHR_RENDERER_AMBIENT_LIGHT.
  //
  // When the background instead needs compositing (an image, or a color
  // with alpha < 1 -- see backgroundNeedsCompositing()), the frame renders
  // on a transparent film: camera rays skip this shader entirely and the
  // output driver composites the background underneath, so only the ambient
  // branch below is ever sampled then.
  auto graph = std::make_unique<ccl::ShaderGraph>();

  auto *lightPath = graph->create_node<ccl::LightPathNode>();

  auto *mix = graph->create_node<ccl::MixNode>();
  mix->set_mix_type(ccl::NODE_MIX_BLEND);
  mix->set_color1(m_ambientRadiance
      * ccl::make_float3(m_ambientColor.x, m_ambientColor.y, m_ambientColor.z));
  mix->set_color2(ccl::make_float3(
      m_backgroundColor.x, m_backgroundColor.y, m_backgroundColor.z));
  graph->connect(lightPath->output("Is Camera Ray"), mix->input("Fac"));

  auto *bg = graph->create_node<ccl::BackgroundNode>();
  graph->connect(mix->output("Color"), bg->input("Color"));

  graph->connect(bg->output("Background"), graph->output()->input("Surface"));

  deviceState()->scene->default_background->name = "anari_default_background";
  deviceState()->scene->default_background->set_graph(std::move(graph));
  deviceState()->scene->default_background->tag_update(deviceState()->scene);
}

// Bake the ARRAY2D 'background' into linear float RGBA for the output
// driver's screen-space composite. Runs under the frame's SceneLock with the
// render thread idle, so it may read the source array safely. Like sampler
// images, the bake keys off the array handle: in-place map/unmap edits of
// the same array are not picked up until the parameter is set again.
void Renderer::rebakeBackgroundImage()
{
  if (!m_backgroundImageArray) {
    m_backgroundImage.reset();
    return;
  }

  const auto type = m_backgroundImageArray->elementType();
  if (!backgroundImageTypeSupported(type)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "renderer -- unsupported element type %s for 'background' image;"
        " ignoring it (default background color applies)",
        anari::toString(type));
    m_backgroundImage.reset();
    return;
  }

  auto image = std::make_shared<BackgroundImage>();
  image->width = uint32_t(m_backgroundImageArray->size(0));
  image->height = uint32_t(m_backgroundImageArray->size(1));
  if (image->width == 0 || image->height == 0) {
    m_backgroundImage.reset();
    return;
  }
  image->texels.resize(size_t(image->width) * image->height);
  const void *data = m_backgroundImageArray->data();
  for (size_t i = 0; i < image->texels.size(); i++)
    image->texels[i] = texelToLinearRGBA(data, type, i);
  m_backgroundImage = std::move(image);
}

void Renderer::makeRendererCurrent()
{
  if (m_needsUpdateStatus.backgroundImage) {
    m_needsUpdateStatus.backgroundImage = false;
    rebakeBackgroundImage();
  }
  if (m_needsUpdateStatus.background || m_needsUpdateStatus.ambientLight) {
    m_needsUpdateStatus.background = false;
    m_needsUpdateStatus.ambientLight = false;
    rebuildDefaultBackgroundShader();
  }
#if defined(WITH_OPTIX) || defined(WITH_OPENIMAGEDENOISE)
  if (m_needsUpdateStatus.denoise) {
    m_needsUpdateStatus.denoise = false;
    reportMessage(ANARI_SEVERITY_DEBUG,
        "renderer -- set_use_denoise(%s)",
        m_denoise ? "true" : "false");
    deviceState()->scene->integrator->set_use_denoise(m_denoise);
    // Cycles' finalize_passes() can only downgrade DENOISED→NOISY (when
    // denoise is off), never upgrade NOISY→DENOISED. Once a named pass
    // becomes NOISY it stays NOISY, causing the output driver to always
    // read the noisy buffer. Fix by restoring DENOISED mode on the named
    // combined pass before the scene update runs.
    if (m_denoise) {
      for (ccl::Pass *pass : deviceState()->scene->passes) {
        if (pass->get_type() == ccl::PASS_COMBINED && !pass->get_name().empty()
            && pass->get_mode() != ccl::PassMode::DENOISED) {
          pass->set_mode(ccl::PassMode::DENOISED);
        }
      }
    }
  }
#else
  (void)m_denoise;
  if (m_needsUpdateStatus.denoise) {
    m_needsUpdateStatus.denoise = false;
    reportMessage(ANARI_SEVERITY_WARNING,
        "renderer -- denoise requested but no denoiser compiled in");
  }
#endif
}

bool Renderer::runAsync() const
{
  return m_runAsync;
}

int Renderer::pixelSamples() const
{
  return m_pixelSamples;
}

math::float3 Renderer::backgroundColor() const
{
  return {m_backgroundColor.x, m_backgroundColor.y, m_backgroundColor.z};
}

math::float4 Renderer::backgroundColorAndAlpha() const
{
  return m_backgroundColor;
}

bool Renderer::backgroundNeedsCompositing() const
{
  return m_backgroundImage != nullptr || m_backgroundColor.w < 1.f;
}

std::shared_ptr<const Renderer::BackgroundImage>
Renderer::backgroundImage() const
{
  return m_backgroundImage;
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Renderer *);
