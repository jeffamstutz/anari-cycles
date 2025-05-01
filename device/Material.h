// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"
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

  ccl::Shader *cyclesShader();

 protected:
  virtual void makeGraph();
  void connectAttributes(ccl::ShaderNode *bsdf,
      const std::string &mode,
      const char *input,
      float v);
  void connectAttributes(ccl::ShaderNode *bsdf,
      const std::string &mode,
      const char *input,
      const float3 &v);

  ccl::Shader *m_shader{nullptr};
  ccl::ShaderGraph *m_graph{nullptr};
  struct Nodes
  {
    ccl::ShaderOutput *attrC{nullptr};
    ccl::ShaderOutput *attr0{nullptr};
    ccl::ShaderOutput *attr1{nullptr};
    ccl::ShaderOutput *attr2{nullptr};
    ccl::ShaderOutput *attr3{nullptr};
    ccl::ShaderOutput *attrC_sc{nullptr};
    ccl::ShaderOutput *attr0_sc{nullptr};
    ccl::ShaderOutput *attr1_sc{nullptr};
    ccl::ShaderOutput *attr2_sc{nullptr};
    ccl::ShaderOutput *attr3_sc{nullptr};
  } m_attributeNodes;

 private:
  void connectAttributesImpl(ccl::ShaderNode *bsdf,
      const std::string &mode,
      const char *input,
      const float3 &v,
      bool singleComponent);
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Material *, ANARI_MATERIAL);
