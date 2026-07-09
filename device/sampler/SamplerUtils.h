// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Sampler.h"
// cycles
#include "util/colorspace.h"
// std
#include <string>

namespace anari_cycles {

// Canonical Cycles attribute name for an ANARI attribute string (the names
// the geometries upload their attribute channels under, see geometry/GeometryAttributes.h).
inline const char *cyclesAttributeName(const std::string &attribute)
{
  if (attribute == "color")
    return "vertex.color";
  if (attribute == "attribute0")
    return "vertex.attribute0";
  if (attribute == "attribute1")
    return "vertex.attribute1";
  if (attribute == "attribute2")
    return "vertex.attribute2";
  if (attribute == "attribute3")
    return "vertex.attribute3";
  return nullptr;
}

inline ccl::ustring imageColorspace(anari::DataType type)
{
  switch (type) {
  case ANARI_UFIXED8_R_SRGB:
  case ANARI_UFIXED8_RA_SRGB:
  case ANARI_UFIXED8_RGB_SRGB:
  case ANARI_UFIXED8_RGBA_SRGB:
    // NOT u_colorspace_srgb: ImageMetaData::finalize() treats that as "needs
    // an OCIO conversion" and silently upgrades BYTE4 storage to HALF4, so
    // the byte pixels load_pixels() writes would be reinterpreted as halfs.
    // scene_linear_srgb keeps byte storage and decodes sRGB in the kernel.
    return ccl::u_colorspace_scene_linear_srgb;
  default:
    return ccl::u_colorspace_data;
  }
}

// Map a single ANARI wrap mode onto the Cycles extension that implements it.
inline ExtensionType cyclesExtension(helium::WrapMode mode)
{
  switch (mode) {
  case helium::WrapMode::REPEAT:
    return EXTENSION_REPEAT;
  case helium::WrapMode::MIRROR_REPEAT:
    return EXTENSION_MIRROR;
  case helium::WrapMode::CLAMP_TO_EDGE:
  default:
    return EXTENSION_EXTEND;
  }
}

// Wrap one texture-coordinate axis in the shader graph so the image can be
// sampled with EXTENSION_REPEAT even when the two axes use different ANARI
// wrap modes (Cycles has a single extension for all axes). Coordinates are
// pre-wrapped into [halfTexel, 1 - halfTexel] (clamp/mirror) or passed
// through (repeat, handled natively by the base extension); keeping the
// filter taps inside the image makes the emulation exact.
inline ccl::ShaderOutput *wrapAxis(ccl::ShaderGraph *graph,
    ccl::ShaderOutput *value,
    helium::WrapMode mode,
    size_t size)
{
  if (mode == helium::WrapMode::REPEAT)
    return value; // native via EXTENSION_REPEAT

  const float halfTexel = 0.5f / float(std::max<size_t>(size, 1));

  if (mode == helium::WrapMode::MIRROR_REPEAT) {
    auto *pingpong = graph->create_node<ccl::MathNode>();
    pingpong->set_math_type(ccl::NODE_MATH_PINGPONG);
    graph->connect(value, pingpong->input("Value1"));
    pingpong->input("Value2")->set(1.f);
    value = pingpong->output("Value");
  }

  auto *lo = graph->create_node<ccl::MathNode>();
  lo->set_math_type(ccl::NODE_MATH_MAXIMUM);
  graph->connect(value, lo->input("Value1"));
  lo->input("Value2")->set(halfTexel);

  auto *hi = graph->create_node<ccl::MathNode>();
  hi->set_math_type(ccl::NODE_MATH_MINIMUM);
  graph->connect(lo->output("Value"), hi->input("Value1"));
  hi->input("Value2")->set(1.f - halfTexel);

  return hi->output("Value");
}

} // namespace anari_cycles
