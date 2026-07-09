// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "sampler/Sampler.h"
// std
#include <map>
// cycles
#include "scene/shader.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"

namespace anari_cycles {

struct Material : public Object
{
  Material(CyclesGlobalState *s);
  virtual ~Material() override;

  static Material *createInstance(
      std::string_view type, CyclesGlobalState *state);

  virtual void finalize() override;

  bool isValid() const override;

  ccl::Shader *cyclesShader();

  // True when the material graph drives the shader's Volume output (interior
  // absorption from PBR thickness/attenuation*). Surfaces use this to warn
  // about geometry that cannot form a closed volume (see Surface::finalize).
  bool hasInteriorVolume() const;

 protected:
  virtual void makeGraph();
  void connectAttributes(ccl::ShaderNode *bsdf,
      const std::string &mode,
      const char *input,
      float v,
      Sampler *sampler = nullptr);
  void connectAttributes(ccl::ShaderNode *bsdf,
      const std::string &mode,
      const char *input,
      const float3 &v,
      Sampler *sampler = nullptr);
  // Wire the "Alpha" input of 'bsdf' honoring alphaMode/alphaCutoff semantics
  // (opaque: force 1, blend: pass through, mask: threshold against cutoff).
  // Per spec the effective alpha is the product of the scalar 'opacity'
  // source and the 4th (alpha) component of the color source, so the color
  // parameter's attribute/sampler is needed here too.
  void connectAlpha(ccl::ShaderNode *bsdf,
      const std::string &opacityAttribute,
      float opacity,
      Sampler *opacitySampler,
      const std::string &colorAttribute,
      Sampler *colorSampler,
      helium::AlphaMode mode,
      float cutoff);

  // Store sampler outputs for reuse
  struct SamplerOutputCache
  {
    Sampler::SamplerOutputs outputs;
    bool isValid{false};
  };
  std::map<Sampler *, SamplerOutputCache> m_samplerOutputs;

  ccl::Shader *m_shader{nullptr};
  // Owned between makeGraph() and Material::finalize(), which hands the
  // completed graph to m_shader. Building the whole graph before
  // Shader::set_graph() matters: set_graph() snapshots e.g.
  // has_volume_connected (which gates KERNEL_FEATURE_VOLUME), so nodes
  // connected after it would not be accounted for.
  std::unique_ptr<ccl::ShaderGraph> m_graphOwned;
  ccl::ShaderGraph *m_graph{nullptr};
  bool m_hasInteriorVolume{false}; // set by subtypes that wire a Volume output
  struct AttributeNodes
  {
    ccl::ShaderOutput *attrC{nullptr};
    ccl::ShaderOutput *attr0{nullptr};
    ccl::ShaderOutput *attr1{nullptr};
    ccl::ShaderOutput *attr2{nullptr};
    ccl::ShaderOutput *attr3{nullptr};
    ccl::ShaderOutput *attrPid{nullptr};
    ccl::ShaderOutput *attrC_sc{nullptr};
    ccl::ShaderOutput *attr0_sc{nullptr};
    ccl::ShaderOutput *attr1_sc{nullptr};
    ccl::ShaderOutput *attr2_sc{nullptr};
    ccl::ShaderOutput *attr3_sc{nullptr};
    ccl::ShaderOutput *attrPid_sc{nullptr};
    // 4th (alpha) component of each attribute; primitiveId is a scalar
    // attribute whose conceptual alpha is the constant 1 (no output needed)
    ccl::ShaderOutput *attrC_a{nullptr};
    ccl::ShaderOutput *attr0_a{nullptr};
    ccl::ShaderOutput *attr1_a{nullptr};
    ccl::ShaderOutput *attr2_a{nullptr};
    ccl::ShaderOutput *attr3_a{nullptr};
  } m_attributeNodes;

  // Get or create sampler outputs for a given sampler
  Sampler::SamplerOutputs getSamplerOutputs(Sampler *sampler);

  // Alpha (4th) component output of an attribute source; nullptr when it is
  // the constant 1
  ccl::ShaderOutput *attributeAlphaOutput(const std::string &attributeSource);

 private:
  void connectAttributesImpl(ccl::ShaderNode *bsdf,
      const std::string &mode,
      Sampler *sampler,
      const char *input,
      const float3 &v,
      bool singleComponent);
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Material *, ANARI_MATERIAL);
