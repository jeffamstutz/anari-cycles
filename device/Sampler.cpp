// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Sampler.h"
#include "Array.h"
#include "cycles_math.h"

namespace anari_cycles {

// Image2D Sampler ////////////////////////////////////////////////////////////

struct Image2D : public Sampler
{
  Image2D(CyclesGlobalState *d);

  bool isValid() const override;
  void commitParameters() override;

  std::unique_ptr<SamplerImageLoader> makeCyclesImageLoader() const override;
  ccl::ImageParams makeCyclesImageParams() const override;

 private:
  helium::IntrusivePtr<Array2D> m_image;
  helium::Attribute m_inAttribute{helium::Attribute::NONE};
  helium::WrapMode m_wrapMode1{helium::WrapMode::DEFAULT};
  helium::WrapMode m_wrapMode2{helium::WrapMode::DEFAULT};
  bool m_linearFilter{true};
  mat4 m_inTransform{mat4(linalg::identity)};
  helium::float4 m_inOffset{0.f, 0.f, 0.f, 0.f};
  mat4 m_outTransform{mat4(linalg::identity)};
  helium::float4 m_outOffset{0.f, 0.f, 0.f, 0.f};
};

Image2D::Image2D(CyclesGlobalState *d) : Sampler(d) {}

bool Image2D::isValid() const
{
  return m_image;
}

void Image2D::commitParameters()
{
  Sampler::commitParameters();
  m_image = getParamObject<Array2D>("image");
  m_inAttribute =
      helium::attributeFromString(getParamString("inAttribute", "attribute0"));
  m_linearFilter = getParamString("filter", "linear") != "nearest";
  m_wrapMode1 =
      helium::wrapModeFromString(getParamString("wrapMode1", "clampToEdge"));
  m_wrapMode2 =
      helium::wrapModeFromString(getParamString("wrapMode2", "clampToEdge"));
  m_inTransform = getParam<mat4>("inTransform", mat4(linalg::identity));
  m_inOffset =
      getParam<helium::float4>("inOffset", helium::float4(0.f, 0.f, 0.f, 0.f));
  m_outTransform = getParam<mat4>("outTransform", mat4(linalg::identity));
  m_outOffset =
      getParam<helium::float4>("outOffset", helium::float4(0.f, 0.f, 0.f, 0.f));
}

std::unique_ptr<SamplerImageLoader> Image2D::makeCyclesImageLoader() const
{
  return std::make_unique<SamplerImageLoader>(m_image.ptr);
}

ccl::ImageParams Image2D::makeCyclesImageParams() const
{
  ccl::ImageParams retval;
  retval.interpolation =
      m_linearFilter ? INTERPOLATION_LINEAR : INTERPOLATION_CLOSEST;
  return retval;
}

// Sampler definitions ////////////////////////////////////////////////////////

Sampler::Sampler(CyclesGlobalState *s) : Object(ANARI_SAMPLER, s) {}

Sampler::~Sampler() = default;

Sampler *Sampler::createInstance(std::string_view subtype, CyclesGlobalState *s)
{
  if (subtype == "image2D")
    return new Image2D(s);
  else
    return (Sampler *)new UnknownObject(ANARI_SAMPLER, subtype, s);
}

std::unique_ptr<SamplerImageLoader> Sampler::makeCyclesImageLoader() const
{
  return {};
}

ccl::ImageParams Sampler::makeCyclesImageParams() const
{
  return {};
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Sampler *);
