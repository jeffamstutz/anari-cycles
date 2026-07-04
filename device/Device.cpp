// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Device.h"
// anari
#include "anari/anari_cpp.hpp"
// cycles
#include "scene/background.h"
#include "scene/film.h"
#include "scene/integrator.h"
#include "scene/shader_nodes.h"

#include "Array.h"
#include "Frame.h"

#include "FrameOutputDriver.h"

namespace anari_cycles {

///////////////////////////////////////////////////////////////////////////////
// Helper functions ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

template <typename HANDLE_T, typename OBJECT_T>
inline HANDLE_T getHandleForAPI(OBJECT_T *object)
{
  return (HANDLE_T)object;
}

template <typename OBJECT_T, typename HANDLE_T, typename... Args>
inline HANDLE_T createObjectForAPI(CyclesGlobalState *s, Args &&...args)
{
  return getHandleForAPI<HANDLE_T>(
      new OBJECT_T(s, std::forward<Args>(args)...));
}

///////////////////////////////////////////////////////////////////////////////
// CyclesDevice definitions ///////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// Data Arrays ////////////////////////////////////////////////////////////////

void *CyclesDevice::mapArray(ANARIArray a)
{
  deviceState()->waitOnCurrentFrame();
  return helium::BaseDevice::mapArray(a);
}

// API Objects ////////////////////////////////////////////////////////////////

ANARIArray1D CyclesDevice::newArray1D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems)
{
  initDevice();

  Array1DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.deleter = deleter;
  md.deleterPtr = userData;
  md.elementType = type;
  md.numItems = numItems;

  if (anari::isObject(type))
    return createObjectForAPI<ObjectArray, ANARIArray1D>(deviceState(), md);
  else
    return createObjectForAPI<Array1D, ANARIArray1D>(deviceState(), md);
}

ANARIArray2D CyclesDevice::newArray2D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2)
{
  initDevice();

  Array2DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.deleter = deleter;
  md.deleterPtr = userData;
  md.elementType = type;
  md.numItems1 = numItems1;
  md.numItems2 = numItems2;

  return createObjectForAPI<Array2D, ANARIArray2D>(deviceState(), md);
}

ANARIArray3D CyclesDevice::newArray3D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3)
{
  initDevice();

  Array3DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.deleter = deleter;
  md.deleterPtr = userData;
  md.elementType = type;
  md.numItems1 = numItems1;
  md.numItems2 = numItems2;
  md.numItems3 = numItems3;

  return createObjectForAPI<Array3D, ANARIArray3D>(deviceState(), md);
}

ANARICamera CyclesDevice::newCamera(const char *subtype)
{
  initDevice();
  return getHandleForAPI<ANARICamera>(
      Camera::createInstance(subtype, deviceState()));
}

ANARIFrame CyclesDevice::newFrame()
{
  initDevice();
  return createObjectForAPI<Frame, ANARIFrame>(deviceState());
}

ANARIGeometry CyclesDevice::newGeometry(const char *subtype)
{
  initDevice();
  return getHandleForAPI<ANARIGeometry>(
      Geometry::createInstance(subtype, deviceState()));
}

ANARIGroup CyclesDevice::newGroup()
{
  initDevice();
  return createObjectForAPI<Group, ANARIGroup>(deviceState());
}

ANARIInstance CyclesDevice::newInstance(const char *subtype)
{
  initDevice();
  return getHandleForAPI<ANARIInstance>(
      Instance::createInstance(subtype, deviceState()));
}

ANARILight CyclesDevice::newLight(const char *subtype)
{
  initDevice();
  // Light constructors create scene nodes -- guard against the render thread.
  CyclesGlobalState::SceneLock sceneLock(*deviceState());
  return getHandleForAPI<ANARILight>(
      Light::createInstance(subtype, deviceState()));
}

ANARIMaterial CyclesDevice::newMaterial(const char *subtype)
{
  initDevice();
  return getHandleForAPI<ANARIMaterial>(
      Material::createInstance(subtype, deviceState()));
}

ANARIRenderer CyclesDevice::newRenderer(const char *subtype)
{
  initDevice();
  return createObjectForAPI<Renderer, ANARIRenderer>(deviceState());
}

ANARISampler CyclesDevice::newSampler(const char *subtype)
{
  initDevice();
  return getHandleForAPI<ANARISampler>(
      Sampler::createInstance(subtype, deviceState()));
}

ANARISpatialField CyclesDevice::newSpatialField(const char *subtype)
{
  initDevice();
  return getHandleForAPI<ANARISpatialField>(
      SpatialField::createInstance(subtype, deviceState()));
}

ANARISurface CyclesDevice::newSurface()
{
  initDevice();
  return createObjectForAPI<Surface, ANARISurface>(deviceState());
}

ANARIVolume CyclesDevice::newVolume(const char *subtype)
{
  initDevice();
  // Volume constructors create scene shaders -- guard against the render
  // thread.
  CyclesGlobalState::SceneLock sceneLock(*deviceState());
  return getHandleForAPI<ANARIVolume>(
      Volume::createInstance(subtype, deviceState()));
}

ANARIWorld CyclesDevice::newWorld()
{
  initDevice();
  return createObjectForAPI<World, ANARIWorld>(deviceState());
}

// Query functions ////////////////////////////////////////////////////////////

const char **CyclesDevice::getObjectSubtypes(ANARIDataType objectType)
{
  return anari_cycles::query_object_types(objectType);
}

const void *CyclesDevice::getObjectInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *infoName,
    ANARIDataType infoType)
{
  return anari_cycles::query_object_info(
      objectType, objectSubtype, infoName, infoType);
}

const void *CyclesDevice::getParameterInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *parameterName,
    ANARIDataType parameterType,
    const char *infoName,
    ANARIDataType infoType)
{
  return anari_cycles::query_param_info(objectType,
      objectSubtype,
      parameterName,
      parameterType,
      infoName,
      infoType);
}

// Object + Parameter Lifetime Management /////////////////////////////////////

int CyclesDevice::getProperty(ANARIObject object,
    const char *name,
    ANARIDataType type,
    void *mem,
    uint64_t size,
    uint32_t mask)
{
  if (mask == ANARI_WAIT) {
    auto lock = scopeLockObject();
    deviceState()->waitOnCurrentFrame();
    // helium::BaseDevice::getProperty() flushes the commit buffer on
    // ANARI_WAIT, which mutates the Cycles scene. Do that flush here under
    // the scene lock instead (the base class flush is then a no-op).
    CyclesGlobalState::SceneLock sceneLock(*deviceState());
    deviceState()->commitBuffer.flush();
  }

  return helium::BaseDevice::getProperty(object, name, type, mem, size, mask);
}

// Other CyclesDevice definitions /////////////////////////////////////////////

CyclesDevice::CyclesDevice(ANARIStatusCallback cb, const void *ptr)
    : helium::BaseDevice(cb, ptr)
{
  m_state = std::make_unique<CyclesGlobalState>(this_device());
  deviceCommitParameters();
}

CyclesDevice::CyclesDevice(ANARILibrary l) : helium::BaseDevice(l)
{
  m_state = std::make_unique<CyclesGlobalState>(this_device());
  deviceCommitParameters();
}

CyclesDevice::~CyclesDevice()
{
  if (m_initialized) {
    auto &state = *deviceState();
    // Stop the completion-callback thread first, while the device, session
    // and scene are all still fully alive: a queued callback must never be
    // invoked with (or release its frame against) a half-destructed device.
    state.output_driver->shutdownCallbackThread();
    state.session->cancel(true);
    state.session->wait();
    state.commitBuffer.clear();
  }

  reportMessage(ANARI_SEVERITY_DEBUG, "destroyed cycles device (%p)", this);
}

int CyclesDevice::deviceGetProperty(const char *name,
    ANARIDataType type,
    void *mem,
    uint64_t size,
    uint32_t mask)
{
  std::string_view prop = name;
  if (prop == "extension" && type == ANARI_STRING_LIST) {
    helium::writeToVoidP(mem, query_extensions());
    return 1;
  } else if (prop == "cycles" && type == ANARI_BOOL) {
    helium::writeToVoidP(mem, true);
    return 1;
  }
  return 0;
}

int CyclesDevice::frameReady(ANARIFrame f, ANARIWaitMask m)
{
  // Deliberately bypasses helium::BaseDevice::frameReady(), which holds the
  // frame's per-object mutex for the duration of the call. With
  // KHR_FRAME_COMPLETION_CALLBACK, frameReady(ANARI_WAIT) must block until
  // the frame's completion callback has returned, and the spec explicitly
  // permits the callback to make ANARI calls (including on this very frame)
  // -- those calls acquire the same per-object mutex, so holding it while
  // blocked here would deadlock them. Frame::frameReady() only touches the
  // FrameOutputDriver, which has its own internal synchronization.
  return helium::referenceFromHandle<helium::BaseFrame>(f).frameReady(m);
}

void CyclesDevice::initDevice()
{
  if (m_initialized)
    return;

  reportMessage(ANARI_SEVERITY_DEBUG, "initializing cycles device (%p)", this);

  auto *forceCPU = getenv("ANARI_CYCLES_FORCE_CPU");

  auto devices = ccl::Device::available_devices();
  ccl::DeviceInfo selectedDevice =
      ccl::Device::available_devices(ccl::DEVICE_MASK_CPU).front();
  for (ccl::DeviceInfo &info : devices) {
    reportMessage(ANARI_SEVERITY_INFO,
        "Found Cycles Device: %-7s| %s",
        ccl::Device::string_from_type(info.type).c_str(),
        info.description.c_str());
    if (!forceCPU && info.type == ccl::DEVICE_OPTIX)
      selectedDevice = info;
    else if (!forceCPU && selectedDevice.type != ccl::DEVICE_OPTIX
        && info.type == ccl::DEVICE_CUDA)
      selectedDevice = info;
  }

  auto &state = *deviceState();

  state.session_params.device = selectedDevice;
  state.session_params.background = false;
  state.session_params.headless = false;
  state.session_params.use_auto_tile = false;
  state.session_params.tile_size = 2048;
  state.session_params.use_resolution_divider = false;
  state.session_params.samples = 1;

#if defined(WITH_OPTIX) || defined(WITH_OPENIMAGEDENOISE)
  if (selectedDevice.type == ccl::DEVICE_OPTIX) {
    state.session_params.denoise_device = selectedDevice;
  } else {
    state.session_params.denoise_device =
        ccl::Device::available_devices(ccl::DEVICE_MASK_CPU).front();
  }
#endif

  reportMessage(ANARI_SEVERITY_INFO,
      "Using Cycles Device '%s'",
      ccl::Device::string_from_type(state.session_params.device.type).c_str());

  state.session =
      std::make_unique<ccl::Session>(state.session_params, state.scene_params);
  state.scene = state.session->scene.get();

  // We cannot use adaptive sampling based on ANARI's async execution model,
  // me _must_ know that the next sample will get executed to trigger completion
  // code signaling the frame is complete.
  state.scene->integrator->set_use_adaptive_sampling(false);

#if defined(WITH_OPTIX) || defined(WITH_OPENIMAGEDENOISE)
  if (selectedDevice.type == ccl::DEVICE_OPTIX) {
    state.scene->integrator->set_denoiser_type(ccl::DENOISER_OPTIX);
  } else {
    state.scene->integrator->set_denoiser_type(ccl::DENOISER_OPENIMAGEDENOISE);
  }
  state.scene->integrator->set_denoiser_passes(
      ccl::DENOISER_PASS_ALBEDO | ccl::DENOISER_PASS_NORMAL);
  state.scene->integrator->set_denoise_use_gpu(false);
  state.scene->integrator->set_denoiser_prefilter(ccl::DENOISER_PREFILTER_FAST);
  state.scene->integrator->set_denoiser_quality(ccl::DENOISER_QUALITY_BALANCED);
  state.scene->integrator->set_use_denoise(false);
#endif

  ccl::Pass *pass_combined = state.scene->create_node<ccl::Pass>();
  pass_combined->set_name(OIIO::ustring("combined"));
  pass_combined->set_type(ccl::PASS_COMBINED);

  ccl::Pass *pass_depth = state.scene->create_node<ccl::Pass>();
  pass_depth->set_name(OIIO::ustring("depth"));
  pass_depth->set_type(ccl::PASS_DEPTH);

  ccl::Pass *pass_normal = state.scene->create_node<ccl::Pass>();
  pass_normal->set_name(OIIO::ustring("normal"));
  pass_normal->set_type(ccl::PASS_NORMAL);

  ccl::Pass *pass_albedo = state.scene->create_node<ccl::Pass>();
  pass_albedo->set_name(OIIO::ustring("diffuse_color"));
  pass_albedo->set_type(ccl::PASS_DIFFUSE_COLOR);

  ccl::Pass *pass_object_id = state.scene->create_node<ccl::Pass>();
  pass_object_id->set_name(OIIO::ustring("object_id"));
  pass_object_id->set_type(ccl::PASS_OBJECT_ID);

  // Value-AOV passes backing the 'primitiveId' and 'instanceId' frame
  // channels. Cycles has no built-in passes for these, so every surface
  // material graph carries OutputAOV nodes writing them (see
  // Material::makeGraph()); the pass names here must match the AOV node
  // names (Film::get_aov_offset() pairs them up by name).
  ccl::Pass *pass_primitive_id = state.scene->create_node<ccl::Pass>();
  pass_primitive_id->set_name(OIIO::ustring("primitiveId"));
  pass_primitive_id->set_type(ccl::PASS_AOV_VALUE);

  ccl::Pass *pass_instance_id = state.scene->create_node<ccl::Pass>();
  pass_instance_id->set_name(OIIO::ustring("instanceId"));
  pass_instance_id->set_type(ccl::PASS_AOV_VALUE);

  // AOVs are written on every hit that still has PATH_RAY_TRANSPARENT_
  // BACKGROUND set and PATH_RAY_SINGLE_PASS_DONE unset. With the Cycles
  // default pass_alpha_threshold (0.5), a hit on a transparent surface
  // (alphaMode 'mask'/'blend') does not set SINGLE_PASS_DONE, so the next
  // surface would write the id AOVs *again* and the accumulated pass would
  // hold the sum of two ids. Threshold 0 makes the very first hit final for
  // all single-write passes, matching the first-hit semantics of the
  // depth/objectId channels.
  state.scene->film->set_pass_alpha_threshold(0.f);

  auto output_driver = std::make_unique<FrameOutputDriver>();
  state.output_driver = output_driver.get();

  state.session->set_output_driver(std::move(output_driver));

  m_initialized = true;
}

CyclesGlobalState *CyclesDevice::deviceState() const
{
  return (CyclesGlobalState *)helium::BaseDevice::m_state.get();
}

} // namespace anari_cycles
