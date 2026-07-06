# Fix: decouple lights from renderer ambient; implement real ambient light

**Type:** P0 bugfix (verified)
**Context:** `../IMPLEMENTATION_STATUS.md` §1.1, §1.2 · setup in `README.md`

## Problem
All analytic lights (point/spot/quad/directional) share Cycles' `scene->default_light`
shader, which `Renderer::rebuildDefaultLightShader()` (device/Renderer.cpp:81) overwrites
with an ambient emission node using magic factors `0.1 * ambientRadiance * 40`. Verified:
`ambientRadiance=0` silently disables every light in the scene; ambient color/intensity
globally tints/scales all lights. Separately, "ambient" never actually illuminates
anything — no light object or background term injects the ambient radiance (the default
background shader kills non-camera rays, Renderer.cpp:43-79).

## Goal
1. Each `Light` subclass owns its own Cycles `Shader` with an emission node (the HDRI light
   at device/Light.cpp:276 already does this — follow that pattern). Cycles applies
   `light->set_strength()` on top; decide whether emission lives in strength or shader, not both.
2. Implement `KHR_RENDERER_AMBIENT_LIGHT` as real illumination: emit
   `ambientColor * ambientRadiance` on the non-camera-ray branch of the default background
   shader (uniform dome), keeping the camera-ray branch showing `background` color.
3. Delete the `0.1` / `40.0` magic constants (Renderer.cpp:32, 88).

## Acceptance
- Quad light at intensity 10, `ambientRadiance=0`: floor clearly lit (was: pitch black).
- No lights, `ambientRadiance=0.25`: floor uniformly lit.
- `ambientColor` change does not alter the color of a white point/quad light's contribution.
- `anariRenderTests` cornell box no longer black *from the lighting side* (full fix also
  needs task 03).
- Add a small behavioral test (see README pattern) to the repo if a test home exists.

## Suggested skills
`/diagnosing-bugs` if the fix misbehaves; `/verify` to exercise renders end-to-end;
`/code-review` before committing.
