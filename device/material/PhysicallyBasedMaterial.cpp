// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "PhysicallyBasedMaterial.h"
// std
#include <algorithm>
#include <cmath>

namespace anari_cycles {

PhysicallyBasedMaterial::PhysicallyBasedMaterial(CyclesGlobalState *s)
    : Material(s),
      m_colorSampler(this),
      m_opacitySampler(this),
      m_roughnessSampler(this),
      m_metallicSampler(this),
      m_normalSampler(this),
      m_clearcoatSampler(this),
      m_clearcoatRoughnessSampler(this),
      m_clearcoatNormalSampler(this),
      m_emissiveSampler(this),
      m_transmissionSampler(this),
      m_iorSampler(this),
      m_specularSampler(this),
      m_specularColorSampler(this),
      m_sheenColorSampler(this),
      m_sheenRoughnessSampler(this),
      m_subsurfaceSampler(this)
{}

void PhysicallyBasedMaterial::commitParameters()
{
  m_colorAttr = getParamString("baseColor", "");
  m_color = getParam<float3>("baseColor", make_float3(1.f, 1.f, 1.f));
  m_colorSampler = getParamObject<Sampler>("baseColor");

  m_opacityAttr = getParamString("opacity", "");
  m_opacity = getParam<float>("opacity", 1.f);
  m_opacitySampler = getParamObject<Sampler>("opacity");

  m_roughnessAttr = getParamString("roughness", "");
  m_roughness = getParam<float>("roughness", 1.f);
  m_roughnessSampler = getParamObject<Sampler>("roughness");

  m_metallicAttr = getParamString("metallic", "");
  m_metallic = getParam<float>("metallic", 1.f);
  m_metallicSampler = getParamObject<Sampler>("metallic");

  m_clearcoatAttr = getParamString("clearcoat", "");
  m_clearcoat = getParam<float>("clearcoat", 0.f);
  m_clearcoatSampler = getParamObject<Sampler>("clearcoat");

  m_clearcoatRoughnessAttr = getParamString("clearcoatRoughness", "");
  m_clearcoatRoughness = getParam<float>("clearcoatRoughness", 0.f);
  m_clearcoatRoughnessSampler = getParamObject<Sampler>("clearcoatRoughness");

  m_clearcoatNormalSampler = getParamObject<Sampler>("clearcoatNormal");

  m_emissiveAttr = getParamString("emissive", "");
  m_emissive = getParam<float3>("emissive", zero_float3());
  m_emissiveSampler = getParamObject<Sampler>("emissive");

  m_transmissionAttr = getParamString("transmission", "");
  m_transmission = getParam<float>("transmission", 0.f);
  m_transmissionSampler = getParamObject<Sampler>("transmission");

  m_iorAttr = getParamString("ior", "");
  m_ior = getParam<float>("ior", 1.5f);
  m_iorSampler = getParamObject<Sampler>("ior");

  // NOTE: the ANARI registry lists 0.0 as the default for 'specular', but the
  // underlying glTF KHR_materials_specular extension (which this parameter
  // mirrors) defaults to 1.0 (i.e. full IOR-derived Fresnel reflection).
  // Defaulting to 0 would strip all specular reflection off every dielectric
  // that doesn't set the parameter, so we follow glTF here (documented in
  // device/json/cycles_khr_material_physically_based.json).
  m_specularAttr = getParamString("specular", "");
  m_specular = getParam<float>("specular", 1.f);
  m_specularSampler = getParamObject<Sampler>("specular");

  m_specularColorAttr = getParamString("specularColor", "");
  m_specularColor = getParam<float3>("specularColor", make_float3(1.f, 1.f, 1.f));
  m_specularColorSampler = getParamObject<Sampler>("specularColor");

  m_sheenColorAttr = getParamString("sheenColor", "");
  m_sheenColor = getParam<float3>("sheenColor", zero_float3());
  m_sheenColorSampler = getParamObject<Sampler>("sheenColor");

  m_sheenRoughnessAttr = getParamString("sheenRoughness", "");
  m_sheenRoughness = getParam<float>("sheenRoughness", 0.f);
  m_sheenRoughnessSampler = getParamObject<Sampler>("sheenRoughness");

  // CYCLES_MATERIAL_SUBSURFACE: random-walk subsurface scattering. Cycles'
  // Principled BSDF derives the subsurface albedo from the base color (there
  // is no separate subsurface color socket anymore), so only the weight,
  // radius, scale, IOR and anisotropy are exposed.
  m_subsurfaceAttr = getParamString("subsurface", "");
  m_subsurface = getParam<float>("subsurface", 0.f);
  m_subsurfaceSampler = getParamObject<Sampler>("subsurface");
  m_subsurfaceRadius =
      getParam<float3>("subsurfaceRadius", make_float3(0.1f, 0.1f, 0.1f));
  m_subsurfaceScale = getParam<float>("subsurfaceScale", 0.1f);
  m_subsurfaceIor = getParam<float>("subsurfaceIor", 1.4f);
  m_subsurfaceAnisotropy = getParam<float>("subsurfaceAnisotropy", 0.f);

  // CYCLES_MATERIAL_EMISSIVE_STRENGTH: scales 'emissive' beyond [0,1] colors
  m_emissiveStrength = getParam<float>("emissiveStrength", 1.f);

  m_iridescence = getParam<float>("iridescence", 0.f);
  m_iridescenceIor = getParam<float>("iridescenceIor", 1.3f);
  m_iridescenceThickness = getParam<float>("iridescenceThickness", 0.f);

  m_thickness = getParam<float>("thickness", 0.f);
  m_attenuationColor =
      getParam<float3>("attenuationColor", make_float3(1.f, 1.f, 1.f));
  m_attenuationDistance = getParam<float>("attenuationDistance", INFINITY);

  m_normalSampler = getParamObject<Sampler>("normal");

  // 'occlusion' is a rasterizer-oriented baked-AO map; a path tracer computes
  // occlusion by actually tracing rays, and Cycles' Principled BSDF has no
  // socket for it. Ignore it (multiplying it into base color would darken
  // directly lit areas incorrectly).
  if (hasParam("occlusion")) {
    if (!m_warnedOcclusion) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "physicallyBased material parameter 'occlusion' is not supported by "
          "the cycles device and will be ignored");
      m_warnedOcclusion = true;
    }
  } else {
    m_warnedOcclusion = false; // warn again if it is set anew later
  }

  m_mode = helium::alphaModeFromString(getParamString("alphaMode", "opaque"));
  m_alphaCutoff = getParam<float>("alphaCutoff", 0.5f);
}

void PhysicallyBasedMaterial::finalize()
{
  makeGraph();

  connectAttributes(
      m_bsdf, m_colorAttr, "Base Color", m_color, m_colorSampler.get());

  connectAlpha(m_bsdf,
      m_opacityAttr,
      m_opacity,
      m_opacitySampler.get(),
      m_colorAttr,
      m_colorSampler.get(),
      m_mode,
      m_alphaCutoff);

  connectAttributes(m_bsdf,
      m_roughnessAttr,
      "Roughness",
      m_roughness,
      m_roughnessSampler.get());

  connectAttributes(
      m_bsdf, m_metallicAttr, "Metallic", m_metallic, m_metallicSampler.get());

  connectAttributes(m_bsdf,
      m_clearcoatAttr,
      "Coat Weight",
      m_clearcoat,
      m_clearcoatSampler.get());
  connectAttributes(m_bsdf,
      m_clearcoatRoughnessAttr,
      "Coat Roughness",
      m_clearcoatRoughness,
      m_clearcoatRoughnessSampler.get());
  connectAttributes(m_bsdf,
      m_emissiveAttr,
      "Emission Color",
      m_emissive,
      m_emissiveSampler.get());
  connectAttributes(m_bsdf,
      m_transmissionAttr,
      "Transmission Weight",
      m_transmission,
      m_transmissionSampler.get());
  connectAttributes(m_bsdf, m_iorAttr, "IOR", m_ior, m_iorSampler.get());

  // ANARI 'specular' scales the dielectric F0; Cycles' "Specular IOR Level"
  // does the same but with a neutral value of 0.5 (IOR-derived Fresnel), so
  // scale by 0.5. Non-constant sources go through a multiply node.
  if (m_specularSampler || !m_specularAttr.empty()) {
    auto *scale = m_graph->create_node<ccl::MathNode>();
    scale->set_math_type(ccl::NODE_MATH_MULTIPLY);
    connectAttributes(
        scale, m_specularAttr, "Value1", m_specular, m_specularSampler.get());
    scale->input("Value2")->set(0.5f);
    m_graph->connect(
        scale->output("Value"), m_bsdf->input("Specular IOR Level"));
  } else {
    m_bsdf->input("Specular IOR Level")->set(0.5f * m_specular);
  }
  connectAttributes(m_bsdf,
      m_specularColorAttr,
      "Specular Tint",
      m_specularColor,
      m_specularColorSampler.get());

  // Cycles splits sheen into weight * tint; ANARI only has sheenColor, so
  // enable the sheen lobe whenever a color source is present (a black
  // constant tint contributes nothing either way).
  const bool sheenActive = m_sheenColorSampler || !m_sheenColorAttr.empty()
      || std::max({m_sheenColor.x, m_sheenColor.y, m_sheenColor.z}) > 0.f;
  m_bsdf->input("Sheen Weight")->set(sheenActive ? 1.f : 0.f);
  connectAttributes(m_bsdf,
      m_sheenColorAttr,
      "Sheen Tint",
      m_sheenColor,
      m_sheenColorSampler.get());
  connectAttributes(m_bsdf,
      m_sheenRoughnessAttr,
      "Sheen Roughness",
      m_sheenRoughness,
      m_sheenRoughnessSampler.get());

  // CYCLES_MATERIAL_SUBSURFACE: the weight supports sampler/attribute
  // sources; radius/scale/IOR/anisotropy are per-material constants. The
  // subsurface albedo is the base color (Cycles has no separate socket).
  connectAttributes(m_bsdf,
      m_subsurfaceAttr,
      "Subsurface Weight",
      m_subsurface,
      m_subsurfaceSampler.get());
  m_bsdf->input("Subsurface Radius")->set(m_subsurfaceRadius);
  m_bsdf->input("Subsurface Scale")->set(m_subsurfaceScale);
  m_bsdf->input("Subsurface IOR")->set(m_subsurfaceIor);
  m_bsdf->input("Subsurface Anisotropy")->set(m_subsurfaceAnisotropy);

  // CYCLES_MATERIAL_EMISSIVE_STRENGTH (makeGraph() already defaults it to 1
  // so plain 'emissive' behaves per the KHR spec)
  m_bsdf->input("Emission Strength")->set(m_emissiveStrength);

  // Cycles' thin-film has no separate weight input (glTF's iridescence factor
  // blends between the plain and the thin-film Fresnel response). Scaling the
  // thickness by the weight would shift the interference hue, so instead any
  // nonzero 'iridescence' enables the film at full strength: hue-correct, but
  // partial weights render stronger than the reference.
  m_bsdf->input("Thin Film Thickness")
      ->set(m_iridescence > 0.f ? m_iridescenceThickness : 0.f);
  m_bsdf->input("Thin Film IOR")->set(m_iridescenceIor);

  // thickness/attenuationColor/attenuationDistance -> interior absorption
  // volume (Beer-Lambert). sigma_t is chosen so that light traveling
  // 'attenuationDistance' through the interior is tinted 'attenuationColor'.
  // The scalar 'thickness' only gates the effect: Cycles integrates along the
  // actual interior path length of the closed geometry. Only wire the volume
  // when transmission can be nonzero -- connecting it unconditionally would
  // enable Cycles' volume kernels/stack for materials no ray can enter.
  const bool mayTransmit = m_transmission > 0.f || m_transmissionSampler
      || !m_transmissionAttr.empty();
  if (mayTransmit && m_thickness > 0.f && m_attenuationDistance > 0.f
      && std::isfinite(m_attenuationDistance)) {
    const float3 sigma = make_float3(
        -std::log(std::clamp(m_attenuationColor.x, 1e-4f, 1.f)),
        -std::log(std::clamp(m_attenuationColor.y, 1e-4f, 1.f)),
        -std::log(std::clamp(m_attenuationColor.z, 1e-4f, 1.f)))
        / m_attenuationDistance;
    const float density = std::max({sigma.x, sigma.y, sigma.z});
    if (density > 0.f) {
      auto *absorption = m_graph->create_node<ccl::AbsorptionVolumeNode>();
      // AbsorptionVolumeNode computes sigma_t = (1 - color) * density
      absorption->set_color(make_float3(1.f - sigma.x / density,
          1.f - sigma.y / density,
          1.f - sigma.z / density));
      absorption->set_density(density);
      m_graph->connect(
          absorption->output("Volume"), m_graph->output()->input("Volume"));
      // Interior volumes only work on closed (mesh-backed) geometry; see the
      // point-cloud limitation warning in Surface::finalize().
      m_hasInteriorVolume = true;
    }
  }

  // Both normal maps use the standard Cycles UV tangent + sign attributes
  // that the geometry uploads from ANARI 'vertex.tangent'/'faceVarying.tangent'
  // (see geometry/GeometryAttributes.h). base=displaced makes the node request exactly those
  // (the default 'original' base requests *_UNDISPLACED variants meant for
  // displacement, which nothing provides here); without displacement both
  // bases are equivalent.
  if (m_normalSampler) {
    auto samplerOutputs = getSamplerOutputs(m_normalSampler.get());
    if (samplerOutputs.colorOutput) {
      auto *normalMap = m_graph->create_node<ccl::NormalMapNode>();
      normalMap->set_space(ccl::NODE_NORMAL_MAP_TANGENT);
      normalMap->set_base(ccl::NODE_NORMAL_MAP_BASE_DISPLACED);
      normalMap->set_attribute(ccl::ustring(""));
      m_graph->connect(samplerOutputs.colorOutput, normalMap->input("Color"));
      m_graph->connect(normalMap->output("Normal"), m_bsdf->input("Normal"));
    }
  }

  if (m_clearcoatNormalSampler) {
    auto samplerOutputs = getSamplerOutputs(m_clearcoatNormalSampler.get());
    if (samplerOutputs.colorOutput) {
      auto *normalMap = m_graph->create_node<ccl::NormalMapNode>();
      normalMap->set_space(ccl::NODE_NORMAL_MAP_TANGENT);
      normalMap->set_base(ccl::NODE_NORMAL_MAP_BASE_DISPLACED);
      normalMap->set_attribute(ccl::ustring(""));
      m_graph->connect(samplerOutputs.colorOutput, normalMap->input("Color"));
      m_graph->connect(
          normalMap->output("Normal"), m_bsdf->input("Coat Normal"));
    }
  }

  Material::finalize();
}

void PhysicallyBasedMaterial::makeGraph()
{
  Material::makeGraph();
  m_bsdf = m_graph->create_node<ccl::PrincipledBsdfNode>();
  m_graph->connect(m_bsdf->output("BSDF"), m_graph->output()->input("Surface"));
  m_bsdf->input("Emission Strength")->set(1.f);
}

} // namespace anari_cycles
