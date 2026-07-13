// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Renderer.h"
// helium
#include "helium/helium_math.h"
// cycles
#include "scene/background.h"
#include "scene/film.h"
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

  // CYCLES_RENDERER_DENOISE_START: accumulated sample count at which
  // 'channel.color' switches from the raw accumulation to the denoised
  // result. Progressive accumulation has no sample limit to count back
  // from, so negative values behave like 0 (denoise from the first sample).
  auto denoiseStart = std::max(0, getParam<int>("denoiseStart", 0));
  m_needsUpdateStatus.denoise |= (m_denoiseStart != denoiseStart);
  m_denoiseStart = denoiseStart;

  // CYCLES_RENDERER_INTERACTIVE_SCALING: low-res preview frames on
  // accumulation resets. No change tracking needed -- a parameter change
  // resets accumulation, and the values are only read per-render by
  // Frame::renderFrame().
  m_interactive.enabled = getParam<bool>("interactiveScaling", true);
  m_interactive.divider =
      std::max(0, getParam<int>("interactiveScalingDivider", 0));
  m_interactive.targetFrameTime = std::max(
      1e-3f, getParam<float>("interactiveScalingTargetFrameTime", 1.f / 30.f));

  // Vendor sampling/integrator controls -- no change tracking needed here:
  // any parameter change resets accumulation, which re-runs
  // makeRendererCurrent()/pushSamplingState(), and the Cycles socket setters
  // themselves no-op when the value is unchanged.
  auto &sp = m_sampling;
  sp.maxBounce = std::max(0, getParam<int>("maxBounce", 7));
  sp.maxDiffuseBounce = std::max(0, getParam<int>("maxDiffuseBounce", 7));
  sp.maxGlossyBounce = std::max(0, getParam<int>("maxGlossyBounce", 7));
  sp.maxTransmissionBounce =
      std::max(0, getParam<int>("maxTransmissionBounce", 7));
  sp.maxVolumeBounce = std::max(0, getParam<int>("maxVolumeBounce", 7));
  sp.maxTransparencyBounce =
      std::max(0, getParam<int>("maxTransparencyBounce", 7));
  sp.clampDirect = std::max(0.f, getParam<float>("clampDirect", 0.f));
  sp.clampIndirect = std::max(0.f, getParam<float>("clampIndirect", 10.f));
  sp.lightTree = getParam<bool>("lightTree", true);
  sp.lightSamplingThreshold =
      std::max(0.f, getParam<float>("lightSamplingThreshold", 0.f));
  sp.causticsReflective = getParam<bool>("causticsReflective", true);
  sp.causticsRefractive = getParam<bool>("causticsRefractive", true);
  sp.filterGlossy = std::max(0.f, getParam<float>("filterGlossy", 0.f));
  sp.aoBounces = std::max(0, getParam<int>("aoBounces", 0));
  sp.aoFactor = std::max(0.f, getParam<float>("aoFactor", 0.f));
  sp.aoDistance =
      std::max(0.f, getParam<float>("aoDistance", 3.402823466e38f));
  sp.adaptiveSampling = getParam<bool>("adaptiveSampling", true);
  sp.adaptiveThreshold =
      std::max(0.f, getParam<float>("adaptiveThreshold", 0.01f));
  sp.adaptiveMinSamples = std::max(0, getParam<int>("adaptiveMinSamples", 0));
  sp.exposure = std::max(0.f, getParam<float>("exposure", 1.f));
  sp.pixelFilter = getParamString("pixelFilter", "box");
  if (sp.pixelFilter != "box" && sp.pixelFilter != "gaussian"
      && sp.pixelFilter != "blackmanHarris") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "renderer -- unknown 'pixelFilter' value '%s';"
        " expected 'box', 'gaussian' or 'blackmanHarris' (using 'box')",
        sp.pixelFilter.c_str());
    sp.pixelFilter = "box";
  }
  sp.pixelFilterWidth =
      std::max(0.01f, getParam<float>("pixelFilterWidth", 1.f));

  // CYCLES_FRAME_CHANNELS: 'channel.mist' distance mapping.
  sp.mistStart = std::max(0.f, getParam<float>("mistStart", 0.f));
  sp.mistDepth = std::max(0.f, getParam<float>("mistDepth", 100.f));
  sp.mistFalloff = std::max(0.f, getParam<float>("mistFalloff", 1.f));
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
    // CYCLES_RENDERER_DENOISE_START: 'denoiseStart' maps to the native
    // integrator socket, which suppresses the denoiser on intermediate
    // scheduler works below the threshold. It cannot suppress the final
    // one -- the Cycles render scheduler unconditionally denoises the last
    // sample of every render, which in this device is the end of every
    // ANARI frame -- so the user-visible gating happens on the read side:
    // the output driver reads the noisy combined pass kept by
    // syncNoisyColorPass() until the threshold is reached.
    deviceState()->scene->integrator->set_denoise_start_sample(m_denoiseStart);
    syncNoisyColorPass();
    // Cycles' finalize_passes() can only downgrade DENOISED→NOISY (when
    // denoise is off), never upgrade NOISY→DENOISED. Once a named pass
    // becomes NOISY it stays NOISY, causing the output driver to always
    // read the noisy buffer. Fix by restoring DENOISED mode on the named
    // combined pass before the scene update runs. Per-lightgroup combined
    // passes are skipped: they don't support denoising (Pass::get_info())
    // and must stay NOISY, and so is the deliberately-noisy pass kept by
    // syncNoisyColorPass().
    if (m_denoise) {
      for (ccl::Pass *pass : deviceState()->scene->passes) {
        if (pass->get_type() == ccl::PASS_COMBINED && !pass->get_name().empty()
            && pass->get_lightgroup().empty()
            && pass->get_name() != g_noisyCombinedPassName
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
  pushSamplingState();
}

// CYCLES_RENDERER_DENOISE_START: while denoising is enabled, keep a named
// NOISY combined pass in the scene. Film's finalize_passes() merges it with
// the auto-generated (unnamed) noisy combined pass -- same type and mode,
// and the merge adopts the name -- so it costs no extra buffer memory; it
// only makes the raw accumulation addressable by name for the output
// driver, which reads it instead of the denoised "combined" pass while the
// accumulated sample count is below 'denoiseStart'. The pass is deleted
// when denoising is off: the named "combined" pass is itself NOISY then,
// and two differently named noisy combined passes would not merge (the
// kernel only writes one of them). Runs under the frame's SceneLock right
// before the session reset that rebuilds the buffer layout.
void Renderer::syncNoisyColorPass()
{
  auto *scene = deviceState()->scene;
  ccl::Pass *noisy = nullptr;
  for (ccl::Pass *pass : scene->passes) {
    if (pass->get_name() == g_noisyCombinedPassName) {
      noisy = pass;
      break;
    }
  }
  if (m_denoise && !noisy) {
    ccl::Pass *pass = scene->create_node<ccl::Pass>();
    pass->set_name(OIIO::ustring(g_noisyCombinedPassName));
    pass->set_type(ccl::PASS_COMBINED);
    pass->set_mode(ccl::PassMode::NOISY);
  } else if (!m_denoise && noisy) {
    scene->delete_node(noisy);
  }
}

// Push the vendor sampling/integrator controls to the shared Cycles
// Integrator/Film. Runs under the frame's SceneLock on every accumulation
// reset; the Cycles setters only tag the nodes modified when a value
// actually changed, so pushing unconditionally is free at defaults and also
// handles switching between renderer objects with different settings.
void Renderer::pushSamplingState()
{
  auto *integrator = deviceState()->scene->integrator;
  auto *film = deviceState()->scene->film;
  const auto &sp = m_sampling;

  integrator->set_max_bounce(sp.maxBounce);
  integrator->set_max_diffuse_bounce(sp.maxDiffuseBounce);
  integrator->set_max_glossy_bounce(sp.maxGlossyBounce);
  integrator->set_max_transmission_bounce(sp.maxTransmissionBounce);
  integrator->set_max_volume_bounce(sp.maxVolumeBounce);
  integrator->set_transparent_max_bounce(sp.maxTransparencyBounce);
  integrator->set_sample_clamp_direct(sp.clampDirect);
  integrator->set_sample_clamp_indirect(sp.clampIndirect);
  integrator->set_use_light_tree(sp.lightTree);
  integrator->set_light_sampling_threshold(sp.lightSamplingThreshold);
  integrator->set_caustics_reflective(sp.causticsReflective);
  integrator->set_caustics_refractive(sp.causticsRefractive);
  integrator->set_filter_glossy(sp.filterGlossy);
  integrator->set_ao_bounces(sp.aoBounces);
  integrator->set_ao_factor(sp.aoFactor);
  integrator->set_ao_distance(sp.aoDistance);
  integrator->set_use_adaptive_sampling(sp.adaptiveSampling);
  integrator->set_adaptive_threshold(sp.adaptiveThreshold);
  integrator->set_adaptive_min_samples(sp.adaptiveMinSamples);

  film->set_exposure(sp.exposure);
  // CYCLES_SURFACE_COMPOSITING 'shadowCatcher': composite the shadow-catcher
  // matte into the combined pass so caught shadows show up directly in
  // 'channel.color' (alpha holds the shadow over a transparent background).
  // Only has an effect when the scene contains a shadow-catcher object.
  film->set_use_approximate_shadow_catcher(true);
  ccl::FilterType filterType = ccl::FILTER_BOX;
  if (sp.pixelFilter == "gaussian")
    filterType = ccl::FILTER_GAUSSIAN;
  else if (sp.pixelFilter == "blackmanHarris")
    filterType = ccl::FILTER_BLACKMAN_HARRIS;
  film->set_filter_type(filterType);
  film->set_filter_width(sp.pixelFilterWidth);
  // CYCLES_FRAME_CHANNELS: 'channel.mist' distance mapping.
  film->set_mist_start(sp.mistStart);
  film->set_mist_depth(sp.mistDepth);
  film->set_mist_falloff(sp.mistFalloff);
}

bool Renderer::runAsync() const
{
  return m_runAsync;
}

int Renderer::pixelSamples() const
{
  return m_pixelSamples;
}

bool Renderer::denoiseEnabled() const
{
#if defined(WITH_OPTIX) || defined(WITH_OPENIMAGEDENOISE)
  return m_denoise;
#else
  return false;
#endif
}

int Renderer::denoiseStart() const
{
  return m_denoiseStart;
}

bool Renderer::interactiveScaling() const
{
  return m_interactive.enabled;
}

int Renderer::interactiveScalingDivider() const
{
  return m_interactive.divider;
}

float Renderer::interactiveScalingTargetFrameTime() const
{
  return m_interactive.targetFrameTime;
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
