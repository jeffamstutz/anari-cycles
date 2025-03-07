// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

// #include "Array.h"
#include "Object.h"
#include "SpatialField.h"

namespace cycles {

struct Volume : public Object
{
  Volume(CyclesGlobalState *s);
  ~Volume() override;

  static Volume *createInstance(std::string_view subtype, CyclesGlobalState *s);

  virtual std::unique_ptr<ccl::Geometry> makeCyclesGeometry() = 0;
  virtual box3 bounds() const = 0;
};

// Subtypes ///////////////////////////////////////////////////////////////////

struct TransferFunction1D : public Volume
{
  TransferFunction1D(CyclesGlobalState *s);
  virtual ~TransferFunction1D() override;

  void commitParameters() override;
  bool isValid() const override;

  std::unique_ptr<ccl::Geometry> makeCyclesGeometry() override;

  box3 bounds() const override;

 private:
  helium::IntrusivePtr<SpatialField> m_field;

  box3 m_bounds;

  helium::box1 m_valueRange{0.f, 1.f};
  float m_densityScale{1.f};

  helium::IntrusivePtr<Array1D> m_colorData;
  helium::IntrusivePtr<Array1D> m_opacityData;

  std::vector<anari_vec::float4> m_rgbaMap;

  ccl::Shader *m_shader{nullptr};
  ccl::ShaderGraph *m_graph{nullptr};

  // Nodes
  ccl::AttributeNode *m_attributeNode{nullptr};
  ccl::MapRangeNode *m_mapRangeNode{nullptr};
  ccl::RGBRampNode *m_rgbRampNode{nullptr};
  ccl::MathNode *m_mathNode{nullptr};

  ccl::PrincipledVolumeNode *m_volumeNode{nullptr};

  ccl::Shader *cyclesShader();
};

} // namespace cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(cycles::Volume *, ANARI_VOLUME);
