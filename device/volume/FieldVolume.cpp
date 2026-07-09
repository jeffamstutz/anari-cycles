// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "FieldVolume.h"
// std
#include <algorithm>
#include <vector>
// cycles
#include "scene/mesh.h"
#include "scene/scene.h"
#include "scene/shader.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"

namespace anari_cycles {

FieldVolume::FieldVolume(CyclesGlobalState *s, const char *shaderName)
    : Volume(s)
{
  auto &state = *deviceState();

  // create_node also sets the node's owner to the scene, which delete_node
  // asserts on
  m_shader = state.scene->create_node<ccl::Shader>();
  m_shader->name = ccl::ustring(shaderName);

  m_shader->set_graph(std::make_unique<ccl::ShaderGraph>());
  m_shader->tag_update(state.scene);
}

FieldVolume::~FieldVolume()
{
  auto &state = *deviceState();
  // Object release can happen while the render thread reads the scene, and
  // scene->objects may still reference the mesh -- defer its deletion.
  CyclesGlobalState::SceneLock sceneLock(state);
  state.retireGeometry(m_mesh);
  // delete_node(Shader*) only clears the reference count; erasing the shader
  // outright would leave a dangling used_shaders entry on the retired mesh.
  state.scene->delete_node(m_shader);
}

ccl::Geometry *FieldVolume::cyclesGeometry() const
{
  return m_mesh;
}

box3 FieldVolume::bounds() const
{
  return m_bounds;
}

void FieldVolume::retireMesh()
{
  deviceState()->retireGeometry(m_mesh);
  m_mesh = nullptr;
}

void FieldVolume::applyVolumeStepRate(const SpatialField *field)
{
  const float3 boundsSize = make_float3(m_bounds.upper[0] - m_bounds.lower[0],
      m_bounds.upper[1] - m_bounds.lower[1],
      m_bounds.upper[2] - m_bounds.lower[2]);
  const float fallbackStep =
      0.1f * ((boundsSize.x + boundsSize.y + boundsSize.z) / 3.f);
  const float desiredStep = field->stepSize();
  if (fallbackStep > 0.f && desiredStep > 0.f) {
    m_shader->set_volume_step_rate(
        ccl::clamp(desiredStep / fallbackStep, 1e-3f, 1.f));
  }
}

void FieldVolume::syncCyclesMesh(
    std::initializer_list<const SpatialField *> fields)
{
  auto &state = *deviceState();

  if (!m_mesh) {
    m_mesh = state.scene->create_node<ccl::Mesh>();
    m_mesh->name = ccl::ustring("ANARI Volume");
  }

  m_mesh->clear(true);

  const auto lo = make_float3(m_bounds.lower[0], m_bounds.lower[1], m_bounds.lower[2]);
  const auto hi = make_float3(m_bounds.upper[0], m_bounds.upper[1], m_bounds.upper[2]);

  const std::vector<float3> vertices{make_float3(lo.x, lo.y, hi.z),
      make_float3(hi.x, lo.y, hi.z),
      make_float3(lo.x, hi.y, hi.z),
      make_float3(hi.x, hi.y, hi.z),
      make_float3(lo.x, lo.y, lo.z),
      make_float3(hi.x, lo.y, lo.z),
      make_float3(lo.x, hi.y, lo.z),
      make_float3(hi.x, hi.y, lo.z)};

  const std::vector<int3> faces{make_int3(0, 1, 2),
      make_int3(2, 1, 3),
      make_int3(1, 5, 3),
      make_int3(3, 5, 7),
      make_int3(5, 4, 7),
      make_int3(7, 4, 6),
      make_int3(4, 0, 6),
      make_int3(6, 0, 2),
      make_int3(2, 3, 6),
      make_int3(6, 3, 7),
      make_int3(5, 4, 1),
      make_int3(1, 4, 0)};

  ccl::array<ccl::float3> P;
  P.resize(vertices.size());
  std::copy(vertices.cbegin(), vertices.cend(), P.begin());
  m_mesh->set_verts(P);

  m_mesh->resize_mesh(int(vertices.size()), int(faces.size()));
  auto *triangles = m_mesh->get_triangles().data();
  auto *shader = m_mesh->get_shader().data();
  auto *smooth = m_mesh->get_smooth().data();
  for (size_t i = 0; i < faces.size(); ++i) {
    const auto &f = faces[i];
    triangles[3 * i + 0] = f.x;
    triangles[3 * i + 1] = f.y;
    triangles[3 * i + 2] = f.z;
    shader[i] = 0;
    smooth[i] = false;
  }
  m_mesh->tag_triangles_modified();
  m_mesh->tag_shader_modified();
  m_mesh->tag_smooth_modified();

  ccl::array<ccl::Node *> used_shaders;
  used_shaders.push_back_slow(m_shader);
  m_mesh->set_used_shaders(used_shaders);

  // Native VDB-backed fields sample a Cycles voxel-grid attribute; the
  // clear() above dropped any previously attached grids, so re-attach them.
  // Each grid image carries its own interpolation mode (the shader-wide
  // volume_interpolation_method stays at its linear default, which the
  // kernel treats as "use the image's interpolation"), so fields with
  // different 'filter' settings coexist in one volume shader.
  for (const SpatialField *field : fields) {
    if (field)
      field->attachVoxelAttributes(m_mesh);
  }

  m_mesh->tag_update(state.scene, true);
}

} // namespace anari_cycles
