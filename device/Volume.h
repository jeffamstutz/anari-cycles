// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "SpatialField.h"

namespace anari_cycles {

struct Volume : public Object
{
  Volume(CyclesGlobalState *s);
  ~Volume() override;

  static Volume *createInstance(std::string_view subtype, CyclesGlobalState *s);

  // Scene-owned geometry node rendered for this volume (may be null when the
  // volume is invalid).
  virtual ccl::Geometry *cyclesGeometry() const = 0;
  virtual box3 bounds() const = 0;

  // User id reported through the 'channel.objectId' frame channel.
  uint32_t id() const;

 protected:
  uint32_t m_id{~0u};
};

// Common base for volume subtypes rendered as a Cycles box proxy mesh (over
// the field bounds) carrying a volume shader.
struct FieldVolume : public Volume
{
  FieldVolume(CyclesGlobalState *s, const char *shaderName);
  ~FieldVolume() override;

  ccl::Geometry *cyclesGeometry() const override;
  box3 bounds() const override;

 protected:
  // (Re)build the scene-owned bounding-box mesh over m_bounds using m_shader.
  // 'fields' are the spatial fields sampled by the shader graph (null entries
  // allowed): rebuilding the mesh drops attached attributes, so each field
  // re-attaches its Cycles voxel-grid image (if any).
  void syncCyclesMesh(std::initializer_list<const SpatialField *> fields);
  // Retire the proxy mesh (invalid-volume path); scene->objects may still
  // reference it, so deletion is deferred.
  void retireMesh();
  // Scale the shader's ray marching step rate to roughly the field's voxel
  // size (without voxel grid attributes Cycles falls back to 1/10th of the
  // object bounds).
  void applyVolumeStepRate(const SpatialField *field);

  box3 m_bounds;
  ccl::Shader *m_shader{nullptr};
  ccl::Mesh *m_mesh{nullptr};
};

// Subtypes ///////////////////////////////////////////////////////////////////

struct TransferFunction1D : public FieldVolume
{
  TransferFunction1D(CyclesGlobalState *s);
  ~TransferFunction1D() override;

  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;

 private:
  void rebuildCyclesShaderGraph();

  helium::ChangeObserverPtr<SpatialField> m_field;

  helium::box1 m_valueRange{0.f, 1.f};
  float m_unitDistance{1.f};

  helium::ChangeObserverPtr<Array1D> m_colorData;
  helium::ChangeObserverPtr<Array1D> m_opacityData;
};

// CYCLES_VOLUME_PRINCIPLED: 'principled' volume subtype driving Cycles'
// Principled Volume closure (scatter + absorption + emission/blackbody) with
// the density sampled from a spatial field -- richer than transferFunction1D
// for smoke/fire.
struct PrincipledVolume : public FieldVolume
{
  PrincipledVolume(CyclesGlobalState *s);
  ~PrincipledVolume() override;

  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;

 private:
  void rebuildCyclesShaderGraph();

  helium::ChangeObserverPtr<SpatialField> m_field;
  helium::ChangeObserverPtr<SpatialField> m_temperatureField;

  float m_densityScale{1.f};
  float3 m_color{make_float3(0.5f, 0.5f, 0.5f)};
  float m_anisotropy{0.f};
  float3 m_absorptionColor{make_float3(0.f, 0.f, 0.f)};
  float m_emissionStrength{0.f};
  float3 m_emissionColor{make_float3(1.f, 1.f, 1.f)};
  float m_blackbodyIntensity{0.f};
  float3 m_blackbodyTint{make_float3(1.f, 1.f, 1.f)};
  float m_temperature{1000.f};
};

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_SPECIALIZATION(anari_cycles::Volume *, ANARI_VOLUME);
