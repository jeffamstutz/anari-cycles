// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "OSLMaterial.h"
#ifdef WITH_OSL
// std
#include <filesystem>
#include <fstream>
// cycles
#include "scene/osl.h"
#include "util/md5.h"
#endif
// std
#include <algorithm>
#include <cmath>

namespace anari_cycles {

OSLMaterial::OSLMaterial(CyclesGlobalState *s) : Material(s) {}

void OSLMaterial::commitParameters()
{
  m_source = getParamString("source", "");
  m_bytecode = getParamString("bytecode", "");
}

bool OSLMaterial::isValid() const
{
  return Material::isValid() && m_oslValid;
}

#ifndef WITH_OSL

void OSLMaterial::finalize()
{
  // Graceful error path: no graph or Cycles shader is ever created, so
  // isValid() stays false and surfaces using this material are skipped.
  m_oslValid = false;
  if (!m_warnedUnsupported) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'osl' material requires a device built with WITH_CYCLES_OSL=ON; "
        "this build has no OSL support, so the material is invalid");
    m_warnedUnsupported = true;
  }
  Object::finalize();
}

#else

void OSLMaterial::finalize()
{
  auto &state = *deviceState();

  m_oslValid = false;

  // The shading system is global per Cycles session; it is only OSL when the
  // build has OSL and the render device supports it (CPU/OptiX), see
  // CyclesDevice::initDevice().
  if (!state.scene->shader_manager->use_osl()) {
    if (!m_warnedUnsupported) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'osl' material requires the OSL shading system, which the "
          "selected Cycles render device does not support (CPU and OptiX "
          "only); the material is invalid");
      m_warnedUnsupported = true;
    }
    Object::finalize();
    return;
  }

  if (m_source.empty() && m_bytecode.empty()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'osl' material requires either 'source' (OSL source code) or "
        "'bytecode' (.oso text); the material is invalid");
    Object::finalize();
    return;
  }

  makeGraph();

  // From here on the (possibly closure-less) graph is always handed to the
  // Cycles shader via Material::finalize() so the scene never holds a shader
  // without a graph; failure paths just leave m_oslValid unset.
  ccl::OSLNode *node = nullptr;
  if (!m_bytecode.empty()) {
    const std::string hash = ccl::util_md5_string(m_bytecode);
    node = ccl::OSLShaderManager::osl_node(
        m_graph, state.scene, "", hash, m_bytecode);
    if (!node) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'osl' material failed to load 'bytecode' (invalid .oso?); the "
          "material is invalid");
    }
  } else {
    // Compile source with oslc into a temp-dir cache keyed by content hash.
    const std::string hash = ccl::util_md5_string(m_source);
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec);
    const fs::path oso = dir / ("anari_cycles_osl_" + hash + ".oso");
    bool compiled = !ec && fs::exists(oso, ec);
    if (!compiled && !ec) {
      const fs::path osl = dir / ("anari_cycles_osl_" + hash + ".osl");
      std::ofstream f(osl, std::ios::trunc);
      f << m_source;
      f.close();
      compiled = f.good()
          && ccl::OSLManager::osl_compile(osl.string(), oso.string());
    }
    if (compiled)
      node = ccl::OSLShaderManager::osl_node(m_graph, state.scene, oso.string());
    if (!node) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'osl' material failed to compile/load 'source' (see oslc output "
          "on stderr); the material is invalid");
    }
  }

  if (node) {
    // Forward equally named ANARI parameters to the shader's input sockets.
    for (ccl::ShaderInput *in : node->inputs) {
      const auto &socket = in->socket_type;
      const char *pname = socket.name.c_str();
      if (!hasParam(pname))
        continue;
      switch (socket.type) {
      case ccl::SocketType::FLOAT:
        node->set(socket, getParam<float>(pname, 0.f));
        break;
      case ccl::SocketType::INT:
        node->set(socket, getParam<int>(pname, 0));
        break;
      case ccl::SocketType::COLOR:
      case ccl::SocketType::VECTOR:
      case ccl::SocketType::POINT:
      case ccl::SocketType::NORMAL:
        node->set(socket, getParam<float3>(pname, zero_float3()));
        break;
      case ccl::SocketType::STRING:
        node->set(socket, ccl::ustring(getParamString(pname, "")));
        break;
      default: // closures et al. cannot be set from ANARI parameters
        break;
      }
    }

    // Wire the first closure output to the surface output.
    ccl::ShaderOutput *closure = nullptr;
    for (ccl::ShaderOutput *out : node->outputs) {
      if (out->socket_type.type == ccl::SocketType::CLOSURE) {
        closure = out;
        break;
      }
    }
    if (closure) {
      m_graph->connect(closure, m_graph->output()->input("Surface"));
      m_oslValid = true;
    } else {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'osl' material shader has no closure output to drive the surface "
          "(only surface shaders are supported); the material is invalid");
    }
  }

  Material::finalize();
}

#endif // WITH_OSL

} // namespace anari_cycles
