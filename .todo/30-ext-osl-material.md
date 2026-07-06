# Vendor ext: OSL shader material subtype

**Type:** vendor extension — highly differentiating, niche
**Context:** `../IMPLEMENTATION_STATUS.md` §4.2 · Cycles: `cycles/src/scene/osl.h`, shader_nodes.h:1677

## Idea
A `CYCLES_MATERIAL_OSL` (or `osl`) material subtype accepting OSL source:
- `source` (STRING — OSL shader source) or `bytecode` (ARRAY1D UINT8 — .oso);
  named params set via generic ANARI params matching the shader's inputs.
- Cycles: `OSLNode::osl_node()` from file/bytecode (osl.h:136), `OSLShaderManager`,
  `OSLCompiler` (osl.h:58-152). GPU support only on OptiX; CPU otherwise.

## Constraints
- Gated on `WITH_OSL` at build time — check current build config first
  (`grep OSL ~/build/claude/anari-cycles/CMakeCache.txt`); may require enabling OSL in the
  vendored Cycles build and adding the dependency to the build recipe.
- Shading system session param must be OSL (session.h:91, `SHADINGSYSTEM_OSL`) — this is
  global per session in Cycles, so mixing OSL and SVM materials needs verification
  (Cycles supports OSL nodes inside SVM? No — shading system is global; document that
  enabling any OSL material switches the session, or reject when mixed).
- Parameter reflection: after compiling, introspect the shader's inputs to map ANARI params.

## Acceptance
A checkerboard-ish OSL source string renders on a mesh via CPU; clear error path when
`WITH_OSL` is off (warn + invalid material, not crash).

## Suggested skills
`/prototype` first (build/mixing constraints); `/grilling` on the API; then `/tdd`,
`/verify`, `/code-review`.
