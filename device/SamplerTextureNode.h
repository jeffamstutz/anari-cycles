// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

// cycles
#include "scene/shader_nodes.h"

CCL_NAMESPACE_BEGIN

struct SamplerTextureNode : public ImageSlotTextureNode
{
  SHADER_NODE_NO_CLONE_CLASS(SamplerTextureNode)
  ShaderNode *clone(ShaderGraph *graph) const override;

 private:
  void cull_tiles(Scene *scene, ShaderGraph *graph);
};

CCL_NAMESPACE_END
