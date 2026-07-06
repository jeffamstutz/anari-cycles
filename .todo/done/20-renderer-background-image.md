# Feature: KHR_RENDERER_BACKGROUND_IMAGE (+ honor background alpha)

**Type:** spec completeness
**Context:** `../IMPLEMENTATION_STATUS.md` §2.3 · registry: `khr_renderer_background_image.json`

## Current state
`background` only accepted as FLOAT32_VEC4 and only RGB used — alpha ignored
(device/Renderer.cpp:24-27, shader at :43-79). Spec extension allows `background` as an
ARRAY2D image (stretched to the frame).

## Cycles mapping
The default background shader already exists (camera-ray branch); an image background needs
an `ImageTextureNode` sampled by screen-space coordinates (window coordinates via
`TextureCoordinateNode`'s Window output) feeding the camera-ray branch.
Alpha: Cycles `Film` transparent-background setting (`use_adaptive_sampling` unrelated;
look at film transparent glass options) controls whether alpha channel shows background —
map `background` alpha < 1 onto transparent film + composite, or document limitation.

## Acceptance
Array2D background shows as image; vec4 background alpha reflected in `channel.color`
alpha; extension declared.

## Suggested skills
`/tdd`; `/verify`; `/code-review`.
