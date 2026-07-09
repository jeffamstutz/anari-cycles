// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Frame.h"
// cycles
#include "scene/background.h"
#include "scene/pass.h"
#include "scene/scene.h"
// std
#include <algorithm>
#include <cstring>

namespace anari_cycles {

// CYCLES_FRAME_CHANNELS: the vendor frame channels this device supports,
// each backed by one Cycles pass created lazily when the channel is first
// requested (see syncAuxPasses()). Notes:
//  - 'channel.mist' distance mapping is driven by the renderer's
//    'mistStart'/'mistDepth'/'mistFalloff' parameters (Cycles Film sockets).
//  - 'channel.motion' holds raw 2D screen-space motion vectors
//    (prev.xy, next.xy); Cycles only computes them while motion blur is
//    inactive (Scene::need_motion() must be MOTION_PASS), so when a
//    non-degenerate camera shutter plus motion transforms enable motion
//    blur the pass reads zero.
//  - 'channel.sampleCount' is read back normalized by the accumulated
//    sample count; the output driver rescales it to absolute per-pixel
//    sample counts (interesting with 'adaptiveSampling' on the renderer).
//  - 'channel.shadowCatcher[Matte]' pair with CYCLES_SURFACE_COMPOSITING's
//    'shadowCatcher' surface flag. Without catcher objects 'shadowCatcher'
//    reads 1 (nothing is shadowed) and the matte pass is not created at all
//    (see syncAuxPasses()), leaving 'channel.shadowCatcherMatte' zero.
static const Frame::AuxChannelDesc g_auxChannelDescs[] = {
    {"channel.position", "position", ccl::PASS_POSITION, ANARI_FLOAT32_VEC3, 3,
        false},
    {"channel.roughness", "roughness", ccl::PASS_ROUGHNESS, ANARI_FLOAT32, 1,
        false},
    {"channel.mist", "mist", ccl::PASS_MIST, ANARI_FLOAT32, 1, false},
    {"channel.motion", "motion", ccl::PASS_MOTION, ANARI_FLOAT32_VEC4, 4,
        false},
    {"channel.sampleCount", "sample_count", ccl::PASS_SAMPLE_COUNT,
        ANARI_FLOAT32, 1, true},
    {"channel.shadowCatcher", "shadow_catcher", ccl::PASS_SHADOW_CATCHER,
        ANARI_FLOAT32_VEC3, 3, false},
    {"channel.shadowCatcherMatte", "shadow_catcher_matte",
        ccl::PASS_SHADOW_CATCHER_MATTE, ANARI_FLOAT32_VEC4, 4, false},
};

// A single flush() advances change-observer chains by only one hop:
// notifications issued during a flush land in the buffer's staging area for
// the *next* flush. Dependency chains span several hops (e.g. a committed
// array region change -- KHR_ARRAY1D_REGION -- re-finalizes the observing
// geometry, which re-finalizes the observing surface, which re-syncs the
// Cycles node), so drain the buffer until stable. The observer graph mirrors
// the acyclic scene graph, so this terminates. Callers must hold the scene
// lock.
static void drainCommitBuffer(CyclesGlobalState &state)
{
  do {
    state.commitBuffer.flush();
  } while (!state.commitBuffer.empty());
}

Frame::Frame(CyclesGlobalState *s) : helium::BaseFrame(s) {}

Frame::~Frame()
{
  // No wait() here: the output driver retains the frame while it is in
  // flight, so reaching this destructor means the frame cannot be rendering.
  // Waiting on the (shared) output driver could deadlock -- e.g. when this
  // frame is destroyed by the commit-buffer flush inside another frame's
  // renderFrame(), after renderBegin() already marked that frame in-flight
  // (and while the scene lock is held).
}

bool Frame::isValid() const
{
  return m_renderer && m_renderer->isValid() && m_camera && m_camera->isValid()
      && m_world && m_world->isValid();
}

CyclesGlobalState *Frame::deviceState() const
{
  return (CyclesGlobalState *)helium::BaseObject::m_state;
}

void Frame::commitParameters()
{
  auto *world = getParamObject<World>("world");
  if (m_world.ptr != world)
    m_worldLastChanged = 0;
  m_world = world;

  m_renderer = getParamObject<Renderer>("renderer");
  m_camera = getParamObject<Camera>("camera");
  m_colorType = getParam<anari::DataType>("channel.color", ANARI_UNKNOWN);
  m_depthType = getParam<anari::DataType>("channel.depth", ANARI_UNKNOWN);
  m_normalType = getParam<anari::DataType>("channel.normal", ANARI_UNKNOWN);
  m_albedoType = getParam<anari::DataType>("channel.albedo", ANARI_UNKNOWN);
  m_objectIdType = getParam<anari::DataType>("channel.objectId", ANARI_UNKNOWN);
  m_primitiveIdType =
      getParam<anari::DataType>("channel.primitiveId", ANARI_UNKNOWN);
  m_instanceIdType =
      getParam<anari::DataType>("channel.instanceId", ANARI_UNKNOWN);

  // CYCLES_LIGHTGROUPS: collect the 'channel.lightgroup.<name>' channels.
  // Only ANARI_FLOAT32_VEC3 is supported -- Cycles per-lightgroup combined
  // passes are RGB (no alpha; see Pass::get_info()).
  m_lightgroupChannels.clear();
  static const std::string lgPrefix = "channel.lightgroup.";
  for (auto p = params_begin(); p != params_end(); ++p) {
    const std::string &pName = p->first;
    if (pName.size() <= lgPrefix.size()
        || pName.compare(0, lgPrefix.size(), lgPrefix) != 0)
      continue;
    const auto type = getParam<anari::DataType>(pName, ANARI_UNKNOWN);
    if (type == ANARI_UNKNOWN)
      continue;
    if (type != ANARI_FLOAT32_VEC3) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'%s' ignored -- lightgroup channels only support "
          "ANARI_FLOAT32_VEC3",
          pName.c_str());
      continue;
    }
    LightgroupChannel ch;
    ch.name = pName.substr(lgPrefix.size());
    ch.passName = "lightgroup_" + ch.name;
    m_lightgroupChannels.push_back(std::move(ch));
  }
  // CYCLES_FRAME_CHANNELS: collect the requested vendor channels. Each one
  // supports exactly one data type (matching the backing Cycles pass).
  m_auxChannels.clear();
  for (const auto &desc : g_auxChannelDescs) {
    const auto type = getParam<anari::DataType>(desc.channel, ANARI_UNKNOWN);
    if (type == ANARI_UNKNOWN)
      continue;
    if (type != desc.type) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'%s' ignored -- only %s is supported",
          desc.channel,
          anari::toString(desc.type));
      continue;
    }
    AuxChannel ch;
    ch.desc = &desc;
    m_auxChannels.push_back(std::move(ch));
  }

  m_accumulation = getParam<bool>("accumulation", false);
  m_completionCallback = getParam<ANARIFrameCompletionCallback>(
      "frameCompletionCallback", nullptr);
  m_completionCallbackUserData =
      getParam<void *>("frameCompletionCallbackUserData", nullptr);
  m_frameData.size = getParam<uint2>("size", make_uint2(10, 10));
}

void Frame::finalize()
{
  if (!m_renderer) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'renderer' on frame");
  }
  if (!m_camera) {
    reportMessage(
        ANARI_SEVERITY_WARNING, "missing required parameter 'camera' on frame");
  }
  if (!m_world) {
    reportMessage(
        ANARI_SEVERITY_WARNING, "missing required parameter 'world' on frame");
  }

  const auto numPixels = m_frameData.size.x * m_frameData.size.y;
  m_perPixelBytes = 4 * (m_colorType == ANARI_FLOAT32_VEC4 ? 4 : 1);
  m_pixelBuffer.resize(numPixels * m_perPixelBytes);
  std::fill(m_pixelBuffer.begin(), m_pixelBuffer.end(), ~0);
  m_depthBuffer.resize(m_depthType == ANARI_FLOAT32 ? numPixels : 0);
  m_normalBuffer.resize(m_normalType == ANARI_FLOAT32_VEC3 ? numPixels * 3 : 0);
  m_albedoBuffer.resize(m_albedoType == ANARI_FLOAT32_VEC3 ? numPixels * 3 : 0);
  m_objectIdBuffer.resize(m_objectIdType == ANARI_UINT32 ? numPixels : 0);
  m_primitiveIdBuffer.resize(m_primitiveIdType == ANARI_UINT32 ? numPixels : 0);
  m_instanceIdBuffer.resize(m_instanceIdType == ANARI_UINT32 ? numPixels : 0);
  for (auto &lg : m_lightgroupChannels)
    lg.buffer.resize(numPixels * 3);
  for (auto &aux : m_auxChannels)
    aux.buffer.resize(size_t(numPixels) * aux.desc->components);
}

bool Frame::getProperty(const std::string_view &name,
    ANARIDataType type,
    void *ptr,
    uint64_t size,
    uint32_t flags)
{
  if (type == ANARI_FLOAT32 && name == "duration") {
    helium::writeToVoidP(ptr, m_duration);
    return true;
  } else if (type == ANARI_FLOAT32 && name == "renderProgress") {
    // Progress of the most recent renderFrame() task: the fraction of the
    // samples that call added on top of the accumulated result. Derived
    // from the session Progress' completed-sample counter, which trails
    // behind this frame's [base, target] sample interval until the render
    // thread catches up (so it never exceeds it and never goes backwards).
    float p = 1.f;
    if (m_progressSampleTarget == 0)
      p = 0.f; // this frame never rendered
    else if (!ready()) {
      auto &progress = deviceState()->session->progress;
      const double done =
          double(progress.get_current_sample()) - double(m_progressSampleBase);
      const double total =
          double(m_progressSampleTarget - m_progressSampleBase);
      p = std::clamp(float(done / total), 0.f, 1.f);
    }
    helium::writeToVoidP(ptr, p);
    return true;
  } else if (type == ANARI_FLOAT32 && name == "refinementProgress") {
    // Progress of the whole (progressive) accumulation toward the current
    // cumulative sample target -- restarts near 0 whenever accumulation
    // resets, reaches 1 when the last requested sample lands.
    float p = 1.f;
    if (m_progressSampleTarget == 0)
      p = 0.f;
    else if (!ready()) {
      p = std::clamp(
          float(deviceState()->session->progress.get_progress()), 0.f, 1.f);
    }
    helium::writeToVoidP(ptr, p);
    return true;
  } else if (type == ANARI_INT32 && name == "numSamples") {
    helium::writeToVoidP(ptr, int(deviceState()->sessionSamples));
    return true;
  } else if (type == ANARI_BOOL && name == "nextFrameReset") {
    if (ready()) {
      CyclesGlobalState::SceneLock lock(*deviceState());
      drainCommitBuffer(*deviceState());
    }
    // After an interactive-scaling preview the next frame always resets
    // (forced full-res restart), even without new scene/camera changes.
    bool doReset = resetAccumulationNextFrame() || m_lastRenderWasPreview;
    helium::writeToVoidP(ptr, doReset);
    return true;
  }

  return false;
}

void Frame::renderFrame()
{
  auto &state = *deviceState();
  state.waitOnCurrentFrame();

  bool currentFrameChanged = state.output_driver->renderBegin(this);

  // Everything from the commit-buffer flush through session reset/sample
  // setup mutates the Cycles scene, which the render session thread reads
  // under scene->mutex -- hold it for the whole section (see SceneLock docs).
  // The lock must be released before any wait on the render thread below.
  {
    CyclesGlobalState::SceneLock sceneLock(state);

    drainCommitBuffer(state);

    if (!isValid()) {
      reportMessage(
          ANARI_SEVERITY_ERROR, "skipping render of incomplete frame object");
      std::fill(m_pixelBuffer.begin(), m_pixelBuffer.end(), 0);
      state.output_driver->renderEnd(); // cycles render thread not going to run
      return;
    }

    // Motion instances bake their motion keys against the camera shutter
    // interval, so a shutter change invalidates the baked scene objects even
    // when the world itself did not change.
    const helium::box1 shutter = m_camera->shutter();
    if (m_worldLastChanged < state.objectUpdates.lastSceneChange
        || m_world->motionRequiresRebake(shutter)) {
      reportMessage(ANARI_SEVERITY_DEBUG, "frame -- updating world");
      m_world->setCyclesWorldObjects(shutter);
      // scene->objects no longer references retired nodes -- safe to delete
      state.purgeRetiredGeometry();
      m_worldLastChanged = helium::newTimeStamp();
    }

    // CYCLES_RENDERER_INTERACTIVE_SCALING: 'changeReset' is the ordinary
    // reset trigger (scene/camera/renderer change, or a different frame
    // object rendering). When the previous render of this frame was a
    // low-res preview, the next unchanged frame forces one extra full-res
    // reset restarting accumulation from sample 0 -- mirroring the Cycles
    // render scheduler's divider step-down to 1. Full-res render durations
    // (previews excluded) seed the automatic divider choice.
    const bool changeReset =
        currentFrameChanged || resetAccumulationNextFrame();
    if (!m_lastRenderWasPreview)
      m_fullResDuration = m_duration;

    if (changeReset || m_lastRenderWasPreview) {
      reportMessage(ANARI_SEVERITY_DEBUG, "frame -- resetting accumulation");

      state.objectUpdates.lastAccumulationReset = helium::newTimeStamp();

      // A change-driven reset with interactive scaling enabled (and only
      // with 'accumulation' on -- without it every frame resets and would
      // stay a low-res preview forever) renders a divider-scaled preview
      // instead of the full-res frame; the output driver upscales it.
      m_preview = PreviewState();
      if (changeReset && m_accumulation && m_renderer->interactiveScaling()) {
        const int divider = choosePreviewDivider();
        if (divider > 1) {
          m_preview.active = true;
          m_preview.divider = divider;
          // Round up (Cycles' divide_up) so odd sizes don't drift the
          // preview's aspect ratio from the frame's.
          const auto d = uint32_t(divider);
          m_preview.size = make_uint2((m_frameData.size.x + d - 1) / d,
              (m_frameData.size.y + d - 1) / d);
          reportMessage(ANARI_SEVERITY_DEBUG,
              "frame -- rendering preview at 1/%i resolution (%u x %u)",
              divider,
              m_preview.size.x,
              m_preview.size.y);
        }
      }
      const uint2 renderSize =
          m_preview.active ? m_preview.size : m_frameData.size;

      m_camera->setCameraCurrent(renderSize.x, renderSize.y);
      m_renderer->makeRendererCurrent();

      // CYCLES_LIGHTGROUPS / CYCLES_FRAME_CHANNELS: bring the scene's
      // per-lightgroup and aux passes in line with this frame's channels
      // before the session reset picks them up.
      syncLightgroupPasses();
      syncAuxPasses();

      // A background-type light (hdri/sky) drives scene->background; when
      // it is not 'visible', its shader shows a solid color to camera rays
      // that must track this renderer's 'background' parameter (no-op
      // otherwise). The pointer is cached during the world rebuild above.
      Light *bgLight = m_world->backgroundLight();
      if (bgLight)
        bgLight->setCameraBackgroundColor(m_renderer->backgroundColor());

      // Background alpha/image (KHR_RENDERER_BACKGROUND_{COLOR,IMAGE}): when
      // camera rays see the renderer background (no camera-visible
      // hdri/sky environment) and it is an image or a color with alpha < 1,
      // render on a transparent film -- camera-path background writes then
      // leave only coverage in the combined pass's alpha -- and have the
      // output driver composite the background (with its alpha) underneath.
      // A visible environment keeps the film opaque (it overrides the
      // renderer background; alpha is 1 everywhere), and so does an opaque
      // background color (the background shader renders it directly and
      // alpha is already 1 everywhere, matching the spec). Non-camera rays
      // are unaffected either way: the ambient dome / environment keeps
      // illuminating the scene.
      const bool compositeBackground = (!bgLight || !bgLight->visibleToCamera())
          && m_renderer->backgroundNeedsCompositing();
      auto *background = state.scene->background;
      if (background->get_transparent() != compositeBackground) {
        background->set_transparent(compositeBackground);
        background->tag_update(state.scene);
      }
      m_bgComposite.enabled = compositeBackground;
      m_bgComposite.color = m_renderer->backgroundColorAndAlpha();
      m_bgComposite.image = m_renderer->backgroundImage();

      state.buffer_params.width = renderSize.x;
      state.buffer_params.height = renderSize.y;
      state.buffer_params.full_width = renderSize.x;
      state.buffer_params.full_height = renderSize.y;

      // The sample target must be in the (delayed) reset params -- a later
      // set_samples() would be clobbered when the reset is applied on the
      // render thread (Session::delayed_reset_buffer_params()).
      state.session_params.samples = m_renderer->pixelSamples();
      state.session->reset(state.session_params, state.buffer_params);
      state.sessionSamples = 0;

      // Session::reset() is applied lazily on the render thread; zero the
      // Progress sample counters right away (thread-safe, and no sampling
      // is in flight here) so the renderProgress/refinementProgress
      // properties don't read the previous accumulation's counts in the
      // window before the reset lands -- stale-high counts would make them
      // spike to 1 and then fall back.
      state.session->progress.reset_sample();
    }

    m_progressSampleBase = state.sessionSamples;
    state.sessionSamples += m_renderer->pixelSamples();
    state.session->set_samples(state.sessionSamples);
    m_progressSampleTarget = state.sessionSamples;

    m_lastRenderWasPreview = m_preview.active;
  }

  state.session->start();

  // NOTE(jda): Everything is still implemented as asynchronous, but on some
  //            machines performance plummets (render thread de-prioritized?),
  //            which doesn't happen if we immediately synchronize.
  //
  // TODO: Investigate how to keep performance and maintain asynchronicity...
  if (!m_renderer->runAsync())
    wait();
}

void *Frame::map(std::string_view channel,
    uint32_t *width,
    uint32_t *height,
    ANARIDataType *pixelType)
{
  wait();

  *width = m_frameData.size.x;
  *height = m_frameData.size.y;

  if (channel == "channel.color") {
    *pixelType = m_colorType;
    return m_pixelBuffer.data();
  } else if (channel == "channel.depth") {
    *pixelType = ANARI_FLOAT32;
    return m_depthBuffer.data();
  } else if (channel == "channel.normal") {
    *pixelType = ANARI_FLOAT32_VEC3;
    return m_normalBuffer.data();
  } else if (channel == "channel.albedo") {
    *pixelType = ANARI_FLOAT32_VEC3;
    return m_albedoBuffer.data();
  } else if (channel == "channel.objectId") {
    *pixelType = ANARI_UINT32;
    return m_objectIdBuffer.data();
  } else if (channel == "channel.primitiveId") {
    *pixelType = ANARI_UINT32;
    return m_primitiveIdBuffer.data();
  } else if (channel == "channel.instanceId") {
    *pixelType = ANARI_UINT32;
    return m_instanceIdBuffer.data();
  } else if (auto it = std::find_if(m_auxChannels.begin(),
                 m_auxChannels.end(),
                 [&](const AuxChannel &aux) {
                   return channel == aux.desc->channel;
                 });
      it != m_auxChannels.end()) {
    *pixelType = it->desc->type;
    return it->buffer.data();
  } else if (channel.rfind("channel.lightgroup.", 0) == 0) {
    const auto name = channel.substr(std::strlen("channel.lightgroup."));
    for (auto &lg : m_lightgroupChannels) {
      if (lg.name == name) {
        *pixelType = ANARI_FLOAT32_VEC3;
        return lg.buffer.data();
      }
    }
  }

  *width = 0;
  *height = 0;
  *pixelType = ANARI_UNKNOWN;
  return nullptr;
}

void Frame::unmap(std::string_view channel)
{
  // no-op
}

int Frame::frameReady(ANARIWaitMask m)
{
  if (m == ANARI_NO_WAIT)
    return ready();
  else {
    // Per KHR_FRAME_COMPLETION_CALLBACK the completion callback must have
    // returned before anariFrameReady(ANARI_WAIT) does.
    deviceState()->output_driver->waitForCallbacks();
    return 1;
  }
}

void Frame::discard()
{
  // no-op
}

bool Frame::ready() const
{
  return deviceState()->output_driver->ready();
}

void Frame::wait() const
{
  deviceState()->output_driver->wait();
}

// Reconcile the scene's per-lightgroup combined passes with this frame's
// 'channel.lightgroup.*' channels: passes for dropped channels are deleted,
// missing ones created. Pass creation/deletion tags the film modified, and
// the scene update then refreshes scene->lightgroups and re-tags the object/
// light managers and background (Scene::device_update()), so objects'
// 'lightGroup' names resolve to the new pass indices automatically. Runs
// under the frame's SceneLock; frames rendered alternately with different
// channel sets re-sync (and restart accumulation) on every switch.
void Frame::syncLightgroupPasses()
{
  auto *scene = deviceState()->scene;

  std::vector<ccl::Pass *> stale;
  std::vector<const LightgroupChannel *> missing;
  for (const auto &lg : m_lightgroupChannels)
    missing.push_back(&lg);

  for (ccl::Pass *pass : scene->passes) {
    if (pass->get_lightgroup().empty())
      continue; // not one of ours -- only this device creates these passes
    auto it = std::find_if(missing.begin(), missing.end(), [&](const auto *lg) {
      return pass->get_name() == lg->passName.c_str();
    });
    if (it != missing.end())
      missing.erase(it);
    else
      stale.push_back(pass);
  }

  for (ccl::Pass *pass : stale)
    scene->delete_node(pass);

  for (const LightgroupChannel *lg : missing) {
    ccl::Pass *pass = scene->create_node<ccl::Pass>();
    pass->set_name(OIIO::ustring(lg->passName));
    pass->set_type(ccl::PASS_COMBINED);
    pass->set_lightgroup(OIIO::ustring(lg->name));
  }
}

// Reconcile the scene's CYCLES_FRAME_CHANNELS aux passes with this frame's
// requested channels, exactly like syncLightgroupPasses() above: passes for
// dropped channels are deleted, missing ones created, so unused channels
// cost nothing. Passes are matched by their (unique) names from
// g_auxChannelDescs -- only this device creates named passes with those
// names (Film's auto-generated helper passes are unnamed). Any helper
// passes a pass needs (e.g. 'motion' -> motion_weight, 'shadow_catcher' ->
// its sample count) are auto-added by Film::update_passes() during the
// scene update, and removed again with it.
//
// One exception: the 'shadow_catcher_matte' pass is only created when the
// scene actually contains shadow-catcher objects. Cycles redirects *every*
// combined-pass read to the matte pass whenever one exists
// (BufferParams::get_actual_display_pass()), and without catcher objects
// the kernel never writes the matte -- 'channel.color' would turn black.
// Runs after setCyclesWorldObjects(), so has_shadow_catcher() is current.
void Frame::syncAuxPasses()
{
  auto *scene = deviceState()->scene;

  std::vector<ccl::Pass *> stale;
  std::vector<const AuxChannelDesc *> missing;
  for (auto &aux : m_auxChannels) {
    const bool active = aux.desc->passType != ccl::PASS_SHADOW_CATCHER_MATTE
        || scene->has_shadow_catcher();
    if (active != aux.active) {
      aux.active = active;
      if (!active) {
        reportMessage(ANARI_SEVERITY_WARNING,
            "'%s' reads zero -- the world has no shadow-catcher surfaces",
            aux.desc->channel);
        std::fill(aux.buffer.begin(), aux.buffer.end(), 0.f);
      }
    }
    if (active)
      missing.push_back(aux.desc);
  }

  for (ccl::Pass *pass : scene->passes) {
    const bool isAuxName = std::any_of(std::begin(g_auxChannelDescs),
        std::end(g_auxChannelDescs),
        [&](const AuxChannelDesc &d) { return pass->get_name() == d.passName; });
    if (!isAuxName)
      continue; // not one of ours
    auto it = std::find_if(missing.begin(), missing.end(), [&](const auto *d) {
      return pass->get_name() == d->passName;
    });
    if (it != missing.end())
      missing.erase(it);
    else
      stale.push_back(pass);
  }

  for (ccl::Pass *pass : stale)
    scene->delete_node(pass);

  for (const AuxChannelDesc *d : missing) {
    ccl::Pass *pass = scene->create_node<ccl::Pass>();
    pass->set_name(OIIO::ustring(d->passName));
    pass->set_type(d->passType);
  }
}

// CYCLES_RENDERER_INTERACTIVE_SCALING: pick the resolution divider for a
// preview frame. A positive 'interactiveScalingDivider' renderer parameter
// is used as-is; otherwise mirror the shape of the Cycles RenderScheduler
// heuristic (render_scheduler.cpp, calculate_resolution_divider_for_time()):
// the smallest power-of-two divider expected to bring the frame under the
// target frame time -- estimated from the last measured full-res duration,
// each halving of the resolution cutting the time by ~4x -- capped at 8 and
// keeping the long image axis at >= 128 pixels. Before any full-res timing
// exists, start at the cap (Blender's start_resolution_divider default).
int Frame::choosePreviewDivider() const
{
  const int fixed = m_renderer->interactiveScalingDivider();
  if (fixed > 0)
    return fixed;

  int divider = 8;
  if (m_fullResDuration > 0.f) {
    divider = 1;
    const float target = m_renderer->interactiveScalingTargetFrameTime();
    for (float estimate = m_fullResDuration; estimate > target && divider < 8;
        estimate *= 0.25f)
      divider *= 2;
  }
  const uint32_t longAxis = std::max(m_frameData.size.x, m_frameData.size.y);
  while (divider > 1 && longAxis / uint32_t(divider) < 128)
    divider /= 2;
  return divider;
}

bool Frame::resetAccumulationNextFrame() const
{
  // Without KHR_FRAME_ACCUMULATION enabled ('accumulation' = false), every
  // anariRenderFrame() renders from scratch instead of refining the previous
  // result.
  if (!m_accumulation)
    return true;
  auto *state = deviceState();
  return state->objectUpdates.lastAccumulationReset
      < state->commitBuffer.lastObjectFinalization();
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Frame *);
