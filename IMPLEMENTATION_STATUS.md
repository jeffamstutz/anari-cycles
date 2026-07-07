# anari-cycles: Implementation Status & Roadmap Notes

*Generated 2026-07-03 by an investigation pass (code audit + ANARI-SDK 0.16.0 registry diff +
Cycles capability survey + live render testing). Intended as a planning basis for future
implementation tasks. Delete or move as desired.*

---

## 1. Verified rendering defects (found by running the device)

All confirmed by rendering with `anariRenderTests` and a minimal hand-written test program
(floor + single light, `ambientRadiance` varied) against a fresh build.

### 1.1 All analytic lights are coupled to the renderer's ambient parameters — BROKEN
- `Renderer::rebuildDefaultLightShader()` (device/Renderer.cpp:81) rewrites Cycles'
  **shared** `scene->default_light` shader with an emission node driven by
  `ambientColor` and `0.1 * ambientRadiance * 40` (note the two magic factors).
- No device light creates its own emission shader (except HDRI), so **every**
  point/spot/quad/directional light uses `default_light` (cycles light.cpp:407,470 falls back
  to it, and `is_enabled` requires nonzero `emission_estimate` on it).
- Measured: with `ambientRadiance=0`, a quad light at intensity 10 contributes **zero**
  (max pixel = background). With `ambientRadiance=0.25` the same quad light works.
- Fix direction: give each `Light` its own Cycles `Shader` with an emission node
  (like HDRI already does), decoupled from ambient.

### 1.2 `KHR_RENDERER_AMBIENT_LIGHT` provides no actual ambient illumination
- There is no light object or background term that injects ambient radiance; the "ambient"
  shader only feeds other lights (see 1.1). Measured: point-light scene with
  `ambientRadiance=0.25` shows the floor entirely black (≤ background).
- The default background shader kills non-camera rays (Renderer.cpp:43-79). Fix direction:
  make the non-camera-ray branch of the background emit `ambientColor * ambientRadiance`
  instead of 0 (gives uniform-dome ambient), or add a hidden background light.

### 1.3 Point light produces no illumination at all
- Even with the shared-shader issue worked around (`ambientRadiance=0.25`), a point light at
  intensity 10 lights nothing while an equivalent quad light works. Root cause not yet
  diagnosed (suspects: strength units, zero radius handling, transform placement).
  Spot/directional need the same behavioral test.

### 1.4 Attribute-driven material color renders black
- All test scenes whose material color comes from vertex attributes render black:
  `demo_cornell_box` (fully black at 64 spp), `test_triangle_attributes`,
  `test_pbr_spheres` (black with red fireflies — possible NaNs).
  A scene with a constant matte color + texture sampler (`test_textured_cube`) renders.
- Suspect: the Cycles `AttributeNode` wiring for `color`/`attribute0..3`
  (Material.cpp:394-421, Geometry.cpp attribute upload) — likely name mismatch between the
  attributes geometry uploads and what the material graph requests.

### 1.5 Volumes are silently never rendered
- `Group::addGroupToCurrentCyclesScene()` (device/Group.cpp:26) creates Cycles objects for
  surfaces and lights only; the committed `volume` array is used solely for bounds.
  `Volume::makeCyclesGeometry()` / `StructuredRegularField::makeCyclesGeometry()` are never
  called on the scene-build path.
- Additionally `VolumeImageLoader::load_metadata/load_pixels` always return `false`
  (device/VolumeImageLoader.cpp:17-28) because Cycles removed dense 3D image textures — a
  NanoVDB-backed loader is needed (see roadmap §4.2).
- Measured: `demo_gravity_spheres_volume` renders a fully empty image.

### 1.6 Runtime introspection is out of sync with the implementation
`anariInfo -l cycles` reports **empty** subtype lists for `ANARI_SAMPLER`,
`ANARI_SPATIAL_FIELD`, `ANARI_VOLUME` even though image1D/image2D samplers,
structuredRegular field, and transferFunction1D volume classes exist. Conversely the
generated quad/triangle parameter info advertises `faceVarying.*` / `primitive.*` parameters
that the code never reads. `device/cycles_device.json` needs a truth pass (see §3).

---

## 2. ANARI 1.1 spec gaps (diff vs SDK 0.16.0 extension registry)

Registry source: `~/opt/anari/share/anari/code_gen/api/*.json`. Device claims 17 extensions
(device/cycles_device.json); the registry defines ~63.

### 2.1 Missing object subtypes
| Category | Implemented | Missing (KHR) | Cycles mapping difficulty |
|---|---|---|---|
| Geometry | triangle, quad, sphere | **cylinder, cone, curve, isosurface**, triangle/quad motion deformation | curve → Cycles `Hair` (easy fit); cylinder/cone → mesh tessellation or hair-with-linear (medium); isosurface → needs volume field first (hard) |
| Camera | perspective, orthographic | **omnidirectional** | trivial — Cycles `CAMERA_PANORAMA` + `PANORAMA_EQUIRECTANGULAR` |
| Light | directional, hdri, point, spot, quad | **ring** | Cycles `AreaLight` with `ellipse=true` (easy) |
| Sampler | image1D, image2D | **image3D, primitive, transform** | transform = pure graph math (easy); primitive = per-prim attribute lookup (medium); image3D blocked by Cycles dropping dense 3D textures (consider NanoVDB conversion) |
| Spatial field | structuredRegular (non-functional) | **nanovdb, unstructured**, structuredRegularCubic | nanovdb is a *direct* fit to Cycles `VDBImageLoader` — do this first |
| Instance | transform (+array) | **motionTransform, motionScaleRotationTranslation** | Cycles objects support motion transform arrays natively (`Object::motion`, up to 129 steps) |
| Material | matte, physicallyBased | — (both exist; param gaps below) | |

### 2.2 Parameter-level gaps in implemented subtypes
(from full code audit; "missing" = never `getParam`ed)

- **Cameras**: `imageRegion` advertised but not read; missing KHR_CAMERA_DEPTH_OF_FIELD
  (`apertureRadius`, `focusDistance` → Cycles `aperturesize`/`focaldistance`, easy),
  KHR_CAMERA_SHUTTER + ROLLING_SHUTTER (Cycles has both), KHR_CAMERA_STEREO (Cycles has
  full stereo incl. spherical), KHR_CAMERA_MOTION_TRANSFORMATION (Cycles camera motion).
- **Geometry (all)**: no `primitive.color`, `primitive.attribute0..3`, `primitive.id`,
  no `faceVarying.*` (triangle/quad), no `vertex.tangent`; `vertex.position` is read without
  element-type validation; sphere attributes drop the 4th component; index arrays accept
  UINT32 only (spec also allows UINT64).
- **matte**: missing `alphaCutoff`.
- **physicallyBased**: missing `specular`, `specularColor`, `sheenColor`, `sheenRoughness`,
  `iridescence(/Ior/Thickness)`, `clearcoatNormal`, `occlusion`, `thickness`,
  `attenuationColor/Distance`, `alphaCutoff`; `emissive`/`transmission`/`clearcoat*` don't
  accept samplers. Nearly all of these map 1:1 onto Cycles Principled BSDF sockets
  (sheen_*, coat_*, specular, thin-film for iridescence) — high-value, low-risk work.
- **Lights**: point/spot missing `power`; directional missing `radiance`+`angularDiameter`
  (Cycles SunLight has `angle`); hdri `visible` read but ignored, `layout` missing;
  KHR_AREA_LIGHTS (`radiance`/`visible` on all lights, Cycles has per-light `is_enabled`,
  camera-visibility flags); quad `side="both"` unsupported; no `intensityDistribution`
  (Cycles has an IES node!).
- **Samplers**: `image2D` `inAttribute` stored but not used (always uses attribute0);
  `mirrorRepeat` silently degrades to extend; `inTransform` drops rotation/shear;
  UFIXED32 pixel formats rejected; image1D doesn't read `inAttribute`/`wrapMode1`.
- **Instance**: `id`, `color`, `attribute0..3` not read (audit) though advertised.
- **Volume**: `color` only as float3 array (spec allows vec4 + scalar forms), `id` missing.

### 2.3 Frame / device-level gaps
- **Channels**: `channel.normal`, `channel.albedo`, `channel.objectId` are wired to Cycles
  passes in code but *not declared*; `channel.primitiveId`, `channel.instanceId` missing.
  `objectId` currently sources from Surface `id` via `pass_id` — check spec semantics.
- **KHR_FRAME_COMPLETION_CALLBACK**: not implemented (no
  `frameCompletionCallback` param). Natural fit: Cycles session end callback.
- **KHR_FRAME_ACCUMULATION**: behavior exists (progressive 1-sample-per-renderFrame,
  Frame.cpp:131) but the extension isn't declared, and there's no `accumulation` param.
- **Frame properties**: `duration`, `numSamples`, `nextFrameReset` exist;
  `renderProgress` / `refinementProgress` missing (Cycles `Progress` has everything needed).
- **KHR_RENDERER_DENOISE**: `denoise` param exists — just declare the KHR name.
- **KHR_RENDERER_BACKGROUND_IMAGE**: missing (renderer `background` ignores alpha too).
- **KHR_ARRAY1D_REGION**: not supported.
- `newRenderer`/`newInstance` ignore requested subtype strings.

---

## 3. Introspection/JSON sync task (quick win)

Regenerate `device/cycles_device.json` to match reality:
1. Add: `khr_sampler_image2D`, and once functional `khr_volume_transferFunction1D` +
   `khr_spatial_field_structured_regular`, `khr_renderer_denoise`,
   `khr_frame_accumulation`, frame channel extensions for the passes already created
   (albedo/normal/objectId).
2. Remove/trim advertised geometry params not actually read (`faceVarying.*`,
   `primitive.attribute*`, `primitive.id`, `vertex.tangent`, instance `color`/`attribute*`,
   camera `imageRegion`) — or implement them.
3. Consider using the SDK's `validate_device.py` + CTS (`anariCts`) as the acceptance check.

---

## 4. Cycles features worth exposing as ANARI extensions

From a survey of `cycles/src/scene/` + `session/`. Ordered roughly by value/effort.

### 4.1 Features that fill KHR extensions (do these as spec work, not vendor exts)
- **NanoVDB volumes** → `KHR_SPATIAL_FIELD_NANOVDB` (Cycles `VDBImageLoader`,
  `WITH_NANOVDB`); also the practical route to make `structuredRegular` work via
  `grid_from_dense_voxels()` (image_vdb.h:57).
- **Curves/hair** → `KHR_GEOMETRY_CURVE` (Cycles `Hair`, ribbon/thick/linear shapes).
- **Motion blur** → `KHR_INSTANCE_MOTION_TRANSFORM`, `KHR_CAMERA_SHUTTER`,
  geometry motion deformation (Cycles motion steps up to 129, `use_motion_blur`).
- **Panoramic/stereo/DoF cameras** → `KHR_CAMERA_OMNIDIRECTIONAL/_STEREO/_DEPTH_OF_FIELD/_SHUTTER`.
- **Ring light** → `KHR_LIGHT_RING` (AreaLight ellipse).
- **IES profiles** → quad/ring `intensityDistribution` (Cycles `IESLightNode`).
- **Denoise** → `KHR_RENDERER_DENOISE` (OIDN/OptiX; needs the build to enable one).

### 4.2 Vendor extension candidates (`CYCLES_`/`EXT_` namespace)
Renderer parameters — **DONE** as `CYCLES_RENDERER_SAMPLING_CONTROLS`
(json/cycles_ext_renderer_sampling_controls.json, Renderer::pushSamplingState()):
bounce controls (`maxBounce` + per-type diffuse/glossy/transmission/volume/transparency),
sample clamping (`clampDirect`/`clampIndirect` — tames indirect fireflies), light tree
toggle + `lightSamplingThreshold`, caustics toggles + `filterGlossy`, fast-GI
(`aoBounces`/`aoFactor`/`aoDistance`), adaptive sampling
(`adaptiveSampling`/`adaptiveThreshold`/`adaptiveMinSamples` — verified to interoperate
with the per-frame accumulation model: Cycles still delivers the render tile when pixels
converge early, so frames complete normally), and Film `exposure` +
`pixelFilter`/`pixelFilterWidth`. All defaults equal the Cycles socket defaults, and the
state is pushed with change-detecting Cycles setters, so unset parameters change nothing.

Still-unexposed renderer candidates:
- **Path guiding** (`use_guiding`) — not compiled in (needs OpenPGL/WITH_PATH_GUIDING);
  expose once the build enables it.
- Session-level `time_limit`, `pixel_size`, `threads`, sample offset/subset
  (checkpoint-style rendering).

Object/scene-level:
- Per-surface visibility flags, **holdout** and **shadow catcher** — **DONE** as
  `CYCLES_SURFACE_COMPOSITING` (json/cycles_ext_surface_compositing.json): Surface
  `visible.camera/diffuse/glossy/transmission/shadow/volumeScatter` (each defaulting to
  the core `visible` param, which is now honored too) map to `Object::visibility`
  PATH_RAY_* bits; `holdout` → `Object::use_holdout`; `shadowCatcher` →
  `Object::is_shadow_catcher` with Film `use_approximate_shadow_catcher` enabled so
  caught shadows composite into `channel.color` alpha (light objects get
  `is_shadow_catcher=true` like Blender so the unshadowed reference sub-path is lit,
  and World re-tags `Scene::tag_shadow_catcher_modified()` on rebuilds since the device
  bypasses `Object::tag_update()`). Flags live on the ANARI Surface, so all instances
  of a surface share them. Not exposed on volumes (core has no Volume `visible`).
  Still-unexposed candidates: shadow-terminator offsets, per-object `ao_distance`,
  caustics caster/receiver.
- **Light linking / shadow linking** — **DONE** as `CYCLES_LIGHT_LINKING`
  (json/cycles_ext_light_linking.json): named link sets on Light
  (`lightSet`/`shadowSet` → `Object::light_set_membership`/`shadow_set_membership`
  one-bit masks) and Surface (`receiverLightSet`/`shadowBlockerSet` →
  `Object::receiver_light_set`/`blocker_shadow_set` indices). Two device-lifetime
  name→index registries (light sets and shadow sets; `CyclesGlobalState::LinkSetRegistry`),
  63 named sets each (index 0 = default set; overflow warns and behaves unset).
  Semantics follow Blender: a linked light illuminates only its set's receivers; a
  receiver in a set is lit by that set's lights plus all unlinked lights (shadow
  linking analogous). Kernel features auto-enable via `Object::has_light_linking()`.
  Verified: two lights/two surfaces exclusive illumination + shadow-set
  membership toggling (/tmp test in task 26). Not exposed on volumes, and emissive
  surfaces don't participate as emitters.
- **Lightgroups** — **DONE** as `CYCLES_LIGHTGROUPS`
  (json/cycles_ext_lightgroups.json): `lightGroup` (STRING) on lights and surfaces →
  `Object::lightgroup`; an HDRI's group also drives `Background::lightgroup`. Frame
  channels `channel.lightgroup.<name>` (ANARI_FLOAT32_VEC3) map to per-lightgroup
  combined passes created/removed on accumulation reset
  (Frame::syncLightgroupPasses(); Scene::device_update() refreshes
  scene->lightgroups and re-tags managers). Lightgroup passes are excluded from the
  denoise pass-mode workaround (they can't be denoised).
- Per-object `color`/`alpha` (already partially there via instance color plumbing).

Geometry:
- **Subdivision surfaces** — **DONE** as `CYCLES_GEOMETRY_SUBDIVISION`
  (json/cycles_ext_geometry_subdivision.json): `subdivision`
  (none/linear/catmullClark), `subdivisionLevel` (dicing cap: 2^level segments
  per patch edge, clamped [0,16], default 12) and `subdivisionDicingRate`
  (object-space target edge length, default 1.0) on triangle/quad geometry,
  plus edge creases via `primitive.creaseIndex` (UINT32/64_VEC2 vertex pairs) +
  `primitive.creaseWeight` (FLOAT32 in [0,1], 1 = fully sharp). Primitives feed
  Cycles' subd-faces path (`Mesh::resize_subd_faces` et al.) and are adaptively
  diced at scene-update time; object-space dicing was chosen over pixel-space
  so results are camera-independent and instancing-safe. Attributes (all five
  channels at faceVarying/vertex/primitive/uniform rates + primitive.id) upload
  to `Mesh::subd_attributes` and are interpolated onto the diced mesh by
  Cycles; user normals/tangents are ignored while subdivision is active (the
  tessellator computes limit-surface normals). Catmull-Clark needs the build
  flag `WITH_CYCLES_OPENSUBDIV=ON` (+`OPENSUBDIV_ROOT_DIR`, e.g.
  ~/opt/OpenSubdiv); without it Cycles dices linearly and the device warns.
  The default remains OFF to keep the first configure dependency-free.
  Verified behaviorally (subdiv_test.cpp): quad cube → sphere-ish blob,
  full-weight creases restore the cube, linear keeps the silhouette, coarse
  vs fine dicing rate visibly changes the silhouette, vertex colors survive
  tessellation, and unsetting `subdivision` restores the plain-triangle path.
  Off by default with zero overhead.
- **Point clouds** (Cycles `PointCloud`) as a faster `sphere` geometry backend.

Materials/shading:
- Subsurface scattering params on physicallyBased (Cycles has full random-walk SSS;
  spec PBR has none — `EXT` territory).
- Principled volume material for `transferFunction1D`-adjacent emission/scatter control.
- Emission strength on matte/PBR beyond `emissive` color.
- **OSL shader material** (`OSLNode`) — a `CYCLES_MATERIAL_OSL` vendor subtype accepting
  shader source strings; niche but very differentiating (CPU/OptiX only).
- Toon/hair BSDF material subtypes.
- Procedural sky (SkyTextureNode) as an `EXT` light subtype (sun+sky in one).

Frame channels (Cycles passes already exist for all of these):
- `mist`, `position`, `roughness`, `motion` vectors, `cryptomatte`,
  `shadow_catcher(+matte)`, AOV color/value, `sample_count` (per-lightgroup
  combined is DONE via `CYCLES_LIGHTGROUPS` `channel.lightgroup.<name>`).
- Variance estimate channel (`channel.colorVariance`-ish) from adaptive sampling buffers.

Device/session:
- Device selection extension: expose CPU/CUDA/OptiX/HIP/Metal/oneAPI choice + GPU index as
  device parameters (currently auto OptiX→CUDA→CPU with `ANARI_CYCLES_FORCE_CPU` env only).
- `pixelSize`, `threads`, progressive resolution divider for interactive use.

---

## 5. Suggested task sequence

1. **Light architecture fix** (defect 1.1 + 1.2): per-light emission shaders; ambient via
   background non-camera-ray term. Removes the `0.1`/`40.0` magic constants. Add behavioral
   tests (the throwaway test at /tmp/anari-cycles-rendertests/quadlight_test.c is a starting
   pattern: render, assert average luminance).
2. **Point light fix** (defect 1.3) + behavioral test matrix over all 5 light types
   (each type alone, ambient=0, assert nonzero floor luminance).
3. **Attribute color plumbing fix** (defect 1.4) — makes cornell box, triangle_attributes,
   pbr_spheres render; then chase remaining fireflies (sample clamp?).
4. **Volume rendering** (defect 1.5): wire volumes into `Group::addGroupToCurrentCyclesScene`,
   implement NanoVDB-backed loader for structuredRegular (dense→grid conversion), declare
   `khr_volume_transferFunction1D` + `khr_spatial_field_structured_regular`; then
   `KHR_SPATIAL_FIELD_NANOVDB` as a fast follow.
5. **Introspection truth pass** (§3) + run `anariCts` and record baseline.
6. **PBR material completion** (§2.2) — sheen/coat/specular/iridescence/SSS sockets.
7. **Geometry subtypes**: curve (Hair), cone/cylinder; primitive.*/faceVarying attributes.
8. **Sampler completion**: transform + primitive samplers, inAttribute selection, mirror wrap,
   UFIXED32 formats.
9. **Camera extensions**: DoF, omnidirectional, shutter, stereo.
10. **Frame completion**: declare existing channels, add primitiveId/instanceId,
    completion callback, renderProgress property, KHR_FRAME_ACCUMULATION.
11. **Cycles vendor extensions** (§4.2) in whatever order serves users: renderer sampling
    controls (DONE: `CYCLES_RENDERER_SAMPLING_CONTROLS`) + denoise first, then
    visibility/holdout/shadow-catcher (DONE: `CYCLES_SURFACE_COMPOSITING`),
    subdivision (DONE: `CYCLES_GEOMETRY_SUBDIVISION`), passes.

## Appendix: how things were tested
```sh
cmake --build ~/build/claude/anari-cycles -j
export ANARI_LIBRARY_PATH=~/build/claude/anari-cycles
export LD_LIBRARY_PATH=~/build/claude/anari-cycles:$HOME/opt/anari/lib
ANARI_LIBRARY=cycles ~/opt/anari/bin/anariInfo -l cycles
ANARI_LIBRARY=cycles ~/opt/anari/bin/anariRenderTests            # writes PNGs to cwd
ANARI_LIBRARY=cycles ~/opt/anari/bin/anariRenderTests -s demo cornell_box --num_samples 64
```
SDK 0.16.0; spec registry at `~/opt/anari/share/anari/code_gen/api/`;
CTS available as `~/opt/anari/bin/anariCts`.
