# anari-cycles

An [ANARI](https://www.khronos.org/anari/) device implementing the
[Cycles](https://www.cycles-renderer.org/) production path tracer from Blender.
This lets any ANARI-based application render with Cycles — physically based
light transport, the Principled BSDF, motion blur, volumes, denoising, and
CPU/GPU (CUDA/OptiX) backends — through the standard, renderer-agnostic ANARI
API. The Cycles sources are vendored as a subproject; the device builds as a
regular ANARI library (`libanari_library_cycles`) loadable via
`ANARI_LIBRARY=cycles`.

See [BUILDING.md](BUILDING.md) for build instructions (including GPU kernels,
denoising, OpenSubdiv, and OpenVDB/NanoVDB options).

## Implemented KHR extensions

Beyond the ANARI 1.0 core objects, the device declares the following official
KHR extensions (some as trimmed local variants that advertise the official
name but omit registry parameters not yet implemented — see
`device/cycles_device.json`):

**Camera**
- `KHR_CAMERA_PERSPECTIVE`, `KHR_CAMERA_ORTHOGRAPHIC`, `KHR_CAMERA_OMNIDIRECTIONAL`
- `KHR_CAMERA_DEPTH_OF_FIELD`, `KHR_CAMERA_SHUTTER`, `KHR_CAMERA_ROLLING_SHUTTER`
- `KHR_CAMERA_STEREO`, `KHR_CAMERA_MOTION_TRANSFORMATION`

**Geometry**
- `KHR_GEOMETRY_TRIANGLE`, `KHR_GEOMETRY_QUAD`, `KHR_GEOMETRY_SPHERE`
- `KHR_GEOMETRY_CYLINDER`, `KHR_GEOMETRY_CONE`, `KHR_GEOMETRY_CURVE`
- `KHR_GEOMETRY_ISOSURFACE`
- `KHR_GEOMETRY_TRIANGLE_MOTION_DEFORMATION`, `KHR_GEOMETRY_QUAD_MOTION_DEFORMATION`

**Instance**
- `KHR_INSTANCE_TRANSFORM`, `KHR_INSTANCE_TRANSFORM_ARRAY`
- `KHR_INSTANCE_MOTION_TRANSFORM`, `KHR_INSTANCE_MOTION_SCALE_ROTATION_TRANSLATION`

**Light**
- `KHR_LIGHT_DIRECTIONAL`, `KHR_LIGHT_POINT`, `KHR_LIGHT_SPOT`
- `KHR_LIGHT_QUAD`, `KHR_LIGHT_RING`, `KHR_LIGHT_HDRI`
- `KHR_AREA_LIGHTS`

**Material**
- `KHR_MATERIAL_MATTE`, `KHR_MATERIAL_PHYSICALLY_BASED`

**Sampler**
- `KHR_SAMPLER_IMAGE1D`, `KHR_SAMPLER_IMAGE2D`
- `KHR_SAMPLER_PRIMITIVE`, `KHR_SAMPLER_TRANSFORM`

**Volume / spatial field**
- `KHR_VOLUME_TRANSFER_FUNCTION1D`
- `KHR_SPATIAL_FIELD_STRUCTURED_REGULAR`, `KHR_SPATIAL_FIELD_STRUCTURED_REGULAR_CUBIC`
- `KHR_SPATIAL_FIELD_NANOVDB`

**Renderer**
- `KHR_RENDERER_AMBIENT_LIGHT`, `KHR_RENDERER_BACKGROUND_COLOR`, `KHR_RENDERER_BACKGROUND_IMAGE`
- `KHR_RENDERER_DENOISE`

**Frame / device**
- `KHR_FRAME_ACCUMULATION`, `KHR_FRAME_COMPLETION_CALLBACK`
- `KHR_FRAME_CHANNEL_ALBEDO`, `KHR_FRAME_CHANNEL_NORMAL`
- `KHR_FRAME_CHANNEL_PRIMITIVE_ID`, `KHR_FRAME_CHANNEL_INSTANCE_ID`, `KHR_FRAME_CHANNEL_OBJECT_ID`
- `KHR_DEVICE_SYNCHRONIZATION`, `KHR_ARRAY1D_REGION`

Notably absent: `KHR_SAMPLER_IMAGE3D` (Cycles has no dense 3D image textures;
creating an `image3D` sampler warns and yields an invalid object) and
`KHR_SPATIAL_FIELD_UNSTRUCTURED` (no Cycles analogue).

## Vendor extensions (`CYCLES_*`)

These expose Cycles capabilities that have no KHR equivalent, or extend a KHR
extension where the standard stops short of what Cycles can do. Introspection
definitions live in `device/json/cycles_ext_*.json`.

### Renderer

- **`CYCLES_RENDERER_SAMPLING_CONTROLS`** — Fine-grained path-tracing
  controls on the renderer: per-type bounce limits, direct/indirect sample
  clamping (firefly suppression), light tree and caustics toggles, glossy
  filtering, fast-GI approximation, adaptive sampling, film exposure, and
  pixel filter selection. The ANARI core renderer only defines
  `pixelSamples`/`ambientColor`-level knobs; production tuning of a path
  tracer needs the rest, and the parameter set is inherently
  Cycles-specific (defaults match the Cycles socket defaults).

- **`CYCLES_RENDERER_INTERACTIVE_SCALING`** — Device-side emulation of the
  Cycles viewport resolution divider: after a camera/scene change, the first
  frame renders at reduced resolution (fixed or automatic divider targeting a
  frame time) and is upscaled, then accumulation restarts at full resolution.
  Gives ~9–40× faster camera-move frames during interaction. No KHR
  extension covers progressive-resolution preview rendering.

- **`CYCLES_RENDERER_DENOISE_START`** — A `denoiseStart` renderer parameter
  (mirroring VisRTX): with denoising enabled, `channel.color` shows the raw
  accumulation until the accumulated sample count reaches the threshold, then
  switches to the denoised result. Extends `KHR_RENDERER_DENOISE`, which
  only offers an on/off toggle — early denoised frames can look smeary, and
  the KHR extension provides no way to delay the denoiser's visible effect.

### Lights and light transport

- **`CYCLES_LIGHT_SKY`** — A `sky` light subtype backed by Cycles' Nishita
  multiple-scattering procedural sky (sun direction/size/intensity, altitude,
  air/dust/ozone density). It drives the scene background like an HDRI light.
  Overlaps in spirit with `KHR_LIGHT_HDRI`, but that extension requires a
  captured environment image; there is no KHR extension for a parametric
  physical sky model.

- **`CYCLES_LIGHT_LINKING`** — Named light/shadow link sets: a light can be
  restricted to illuminate (or cast shadows on) only surfaces that opt into
  its set, following Blender's light-linking semantics. Pure
  artistic-control feature with no KHR counterpart.

- **`CYCLES_LIGHTGROUPS`** — A `lightGroup` name on lights and surfaces plus
  per-group frame channels (`channel.lightgroup.<name>`) that decompose the
  beauty image into per-group lighting contributions for downstream
  compositing. No KHR extension defines light AOVs.

### Surfaces and geometry

- **`CYCLES_SURFACE_COMPOSITING`** — Per-surface ray-visibility flags
  (camera/diffuse/glossy/transmission/shadow/volume scatter), holdout
  objects, and shadow catchers whose caught shadows composite into the color
  channel's alpha. The ANARI core only has a boolean `visible`; per-ray-type
  visibility and catcher/holdout workflows are compositing features KHR does
  not cover.

- **`CYCLES_GEOMETRY_SUBDIVISION`** — Catmull-Clark or linear subdivision on
  triangle/quad geometry with adaptive object-space dicing and edge creases,
  using Cycles' OpenSubdiv path. KHR geometry extensions describe only
  fixed, pre-tessellated primitives; render-time subdivision has no KHR
  equivalent.

### Materials and shading

- **`CYCLES_MATERIAL_SUBSURFACE`** — Random-walk subsurface scattering
  parameters (`subsurface` weight, per-channel radius, scale, IOR,
  anisotropy) on the `physicallyBased` material. Extends
  `KHR_MATERIAL_PHYSICALLY_BASED`, which defines no subsurface parameters at
  all.

- **`CYCLES_MATERIAL_EMISSIVE_STRENGTH`** — An `emissiveStrength` scalar
  multiplier on `physicallyBased`, mirroring glTF's
  `KHR_materials_emissive_strength`. Extends
  `KHR_MATERIAL_PHYSICALLY_BASED`, whose `emissive` is a [0,1] color — the
  scale factor allows emitted radiance beyond that range.

- **`CYCLES_MATERIAL_TOON`** — A `toon` material subtype exposing Cycles'
  Toon BSDF (diffuse/glossy component, size, smooth) for non-photorealistic
  rendering. KHR only defines `matte` and `physicallyBased`; a stylized
  BSDF cannot be expressed through either.

- **`CYCLES_MATERIAL_HAIR`** — A `hair` material subtype exposing Cycles'
  Principled Hair BSDF (Huang/Chiang models, melanin-based coloring,
  roughness controls); pairs with `KHR_GEOMETRY_CURVE` strands. Hair fiber
  scattering is physically distinct from a surface BSDF and has no KHR
  material.

- **`CYCLES_MATERIAL_OSL`** — An `osl` material subtype accepting Open
  Shading Language source (compiled at commit time) or precompiled `.oso`
  bytecode, with shader inputs settable as ANARI parameters. Arbitrary
  programmable shading is far beyond any KHR material. Requires a build
  with `WITH_CYCLES_OSL=ON`; otherwise the material warns and is invalid.

### Volumes

- **`CYCLES_VOLUME_PRINCIPLED`** — A `principled` volume subtype driving
  Cycles' Principled Volume node: density from a spatial field, scattering
  albedo, anisotropy, absorption, emission, and blackbody fire (with an
  optional temperature field). Overlaps with
  `KHR_VOLUME_TRANSFER_FUNCTION1D` in that both render spatial fields, but
  the KHR volume is a colormapped/scientific-visualization model; this one
  is a physically based smoke/fire shading model that a transfer function
  cannot express.

### Frame and device

- **`CYCLES_FRAME_CHANNELS`** — Additional render passes as frame channels:
  `position`, `roughness`, `mist` (with renderer mist controls), 2D `motion`
  vectors, per-pixel `sampleCount`, `shadowCatcher`, and
  `shadowCatcherMatte`. Extends the `KHR_FRAME_CHANNEL_*` family, which
  stops at albedo/normal/ids — these are the extra AOVs compositors expect
  from a production renderer.

- **`CYCLES_DEVICE_SELECTION`** — Device parameters `computeDevice`
  (auto/cpu/cuda/optix/hip/metal/oneapi) and `computeDeviceIndex` to pick
  the Cycles compute backend, plus read-only properties listing available
  backends and reporting the one in use. ANARI has no standard way to
  select a backend within a device implementation.
