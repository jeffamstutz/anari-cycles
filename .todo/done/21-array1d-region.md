# Feature: KHR_ARRAY1D_REGION

**Type:** spec completeness — small
**Context:** `../IMPLEMENTATION_STATUS.md` §2.3 · registry: `khr_array1d_region.json`

## Spec surface
`region` param (ANARI_UINT64_REGION1) on ANARI_ARRAY1D: restricts the *valid* region of the
array without re-creating it; consumers must observe only [begin, end). Committing a new
region must trigger re-finalization of dependent objects.

## Current state
Arrays are stock helium types (device/Array.h:14-25; device/Device.cpp:42-111). helium may
already have region support — check `helium::Array1D` in the ANARI-SDK source
(~/src/ANARI-SDK/src/helium) before writing anything; if helium handles it, this may be
mostly declaring the extension + auditing consumers to use `size()`-after-region rather
than raw numItems. Note audit finding: many consumers hold plain `IntrusivePtr` instead of
`ChangeObserverPtr` (e.g. vertex.color, sampler image, volume color/opacity), so region
updates would not re-trigger commits — fix observation while in here.

## Acceptance
Growing/shrinking region on a position array changes the rendered primitive count without
re-creating the array; extension declared.

## Suggested skills
`/tdd`; `/code-review`.
