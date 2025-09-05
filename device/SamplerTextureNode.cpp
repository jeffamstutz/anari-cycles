// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "SamplerTextureNode.h"
// cycles
#include "scene/mesh.h"
#include "scene/osl.h"
#include "scene/scene.h"
#include "scene/svm.h"

CCL_NAMESPACE_BEGIN

#define TEXTURE_MAPPING_DEFINE(TextureNode)                                    \
  SOCKET_POINT(tex_mapping.translation, "Translation", zero_float3());         \
  SOCKET_VECTOR(tex_mapping.rotation, "Rotation", zero_float3());              \
  SOCKET_VECTOR(tex_mapping.scale, "Scale", one_float3());                     \
                                                                               \
  SOCKET_VECTOR(                                                               \
      tex_mapping.min, "Min", make_float3(-FLT_MAX, -FLT_MAX, -FLT_MAX));      \
  SOCKET_VECTOR(                                                               \
      tex_mapping.max, "Max", make_float3(FLT_MAX, FLT_MAX, FLT_MAX));         \
  SOCKET_BOOLEAN(tex_mapping.use_minmax, "Use Min Max", false);                \
                                                                               \
  static NodeEnum mapping_axis_enum;                                           \
  mapping_axis_enum.insert("none", TextureMapping::NONE);                      \
  mapping_axis_enum.insert("x", TextureMapping::X);                            \
  mapping_axis_enum.insert("y", TextureMapping::Y);                            \
  mapping_axis_enum.insert("z", TextureMapping::Z);                            \
  SOCKET_ENUM(tex_mapping.x_mapping,                                           \
      "x_mapping",                                                             \
      mapping_axis_enum,                                                       \
      TextureMapping::X);                                                      \
  SOCKET_ENUM(tex_mapping.y_mapping,                                           \
      "y_mapping",                                                             \
      mapping_axis_enum,                                                       \
      TextureMapping::Y);                                                      \
  SOCKET_ENUM(tex_mapping.z_mapping,                                           \
      "z_mapping",                                                             \
      mapping_axis_enum,                                                       \
      TextureMapping::Z);                                                      \
                                                                               \
  static NodeEnum mapping_type_enum;                                           \
  mapping_type_enum.insert("point", TextureMapping::POINT);                    \
  mapping_type_enum.insert("texture", TextureMapping::TEXTURE);                \
  mapping_type_enum.insert("vector", TextureMapping::VECTOR);                  \
  mapping_type_enum.insert("normal", TextureMapping::NORMAL);                  \
  SOCKET_ENUM(                                                                 \
      tex_mapping.type, "Type", mapping_type_enum, TextureMapping::TEXTURE);   \
                                                                               \
  static NodeEnum mapping_projection_enum;                                     \
  mapping_projection_enum.insert("flat", TextureMapping::FLAT);                \
  mapping_projection_enum.insert("cube", TextureMapping::CUBE);                \
  mapping_projection_enum.insert("tube", TextureMapping::TUBE);                \
  mapping_projection_enum.insert("sphere", TextureMapping::SPHERE);            \
  SOCKET_ENUM(tex_mapping.projection,                                          \
      "Projection",                                                            \
      mapping_projection_enum,                                                 \
      TextureMapping::FLAT);

///////////////////////////////////////////////////////////////////////////////

NODE_DEFINE(SamplerTextureNode)
{
  NodeType *type =
      NodeType::add("anari_sampler_texture", create, NodeType::SHADER);
  TEXTURE_MAPPING_DEFINE(SamplerTextureNode);
  return type;
}

ShaderNode *SamplerTextureNode::clone(ShaderGraph *graph) const
{
  SamplerTextureNode *node = graph->create_node<SamplerTextureNode>(*this);
  node->handle = handle;
  return node;
}

void SamplerTextureNode::compile(SVMCompiler &compiler)
{
  ShaderInput *vector_in = input("Vector");
  ShaderOutput *color_out = output("Color");
  ShaderOutput *alpha_out = output("Alpha");

  if (handle.empty()) {
    cull_tiles(compiler.scene, compiler.current_graph);
#if 0
    ImageManager *image_manager = compiler.scene->image_manager.get();
    handle = image_manager->add_image(filename.string(), image_params(), tiles);
#endif
  }

  /* All tiles have the same metadata. */
  const ImageMetaData metadata = handle.metadata();
  const bool compress_as_srgb = metadata.compress_as_srgb;

  const int vector_offset = tex_mapping.compile_begin(compiler, vector_in);
  uint flags = 0;

  if (compress_as_srgb) {
    flags |= NODE_IMAGE_COMPRESS_AS_SRGB;
  }

  // NOTE(jda): hard coded modes for interpreting alpha and projection
  const auto alpha_type = IMAGE_ALPHA_ASSOCIATED;
  const auto projection = NODE_IMAGE_PROJ_FLAT;

  if (!alpha_out->links.empty()) {
    const bool unassociate_alpha =
        !(ColorSpaceManager::colorspace_is_data(metadata.colorspace)
            || alpha_type == IMAGE_ALPHA_CHANNEL_PACKED
            || alpha_type == IMAGE_ALPHA_IGNORE);

    if (unassociate_alpha) {
      flags |= NODE_IMAGE_ALPHA_UNASSOCIATE;
    }
  }

  if (projection != NODE_IMAGE_PROJ_BOX) {
    /* If there only is one image (a very common case), we encode it as a
     * negative value. */
    int num_nodes;
    if (handle.num_tiles() == 0) {
      num_nodes = -handle.svm_slot();
    } else {
      num_nodes = divide_up(handle.num_tiles(), 2);
    }

    compiler.add_node(NODE_TEX_IMAGE,
        num_nodes,
        compiler.encode_uchar4(vector_offset,
            compiler.stack_assign_if_linked(color_out),
            compiler.stack_assign_if_linked(alpha_out),
            flags),
        projection);

    if (num_nodes > 0) {
      for (int i = 0; i < num_nodes; i++) {
        int4 node;
        node.x = tiles[2 * i];
        node.y = handle.svm_slot(2 * i);
        if (2 * i + 1 < tiles.size()) {
          node.z = tiles[2 * i + 1];
          node.w = handle.svm_slot(2 * i + 1);
        } else {
          node.z = -1;
          node.w = -1;
        }
        compiler.add_node(node.x, node.y, node.z, node.w);
      }
    }
  } else {
    assert(handle.num_svm_slots() == 1);
    compiler.add_node(NODE_TEX_IMAGE_BOX,
        handle.svm_slot(),
        compiler.encode_uchar4(vector_offset,
            compiler.stack_assign_if_linked(color_out),
            compiler.stack_assign_if_linked(alpha_out),
            flags),
        __float_as_int(projection_blend));
  }

  tex_mapping.compile_end(compiler, vector_in, vector_offset);
}

void SamplerTextureNode::compile(OSLCompiler &compiler)
{
#if 0
  ShaderOutput *alpha_out = output("Alpha");

  tex_mapping.compile(compiler);

  if (handle.empty()) {
    ImageManager *image_manager = compiler.scene->image_manager.get();
    handle = image_manager->add_image(filename.string(), image_params());
  }

  const ImageMetaData metadata = handle.metadata();
  const bool is_float = metadata.is_float();
  const bool compress_as_srgb = metadata.compress_as_srgb;
  const ustring known_colorspace = metadata.colorspace;

  if (handle.svm_slot() == -1) {
    compiler.parameter_texture("filename",
        filename,
        compress_as_srgb ? u_colorspace_raw : known_colorspace);
  } else {
    compiler.parameter_texture("filename", handle);
  }

  const bool unassociate_alpha =
      !(ColorSpaceManager::colorspace_is_data(colorspace)
          || alpha_type == IMAGE_ALPHA_CHANNEL_PACKED
          || alpha_type == IMAGE_ALPHA_IGNORE);
  const bool is_tiled = (filename.find("<UDIM>") != string::npos
                            || filename.find("<UVTILE>") != string::npos)
      || handle.num_tiles() > 0;

  compiler.parameter(this, "projection");
  compiler.parameter(this, "projection_blend");
  compiler.parameter("compress_as_srgb", compress_as_srgb);
  compiler.parameter("ignore_alpha", alpha_type == IMAGE_ALPHA_IGNORE);
  compiler.parameter(
      "unassociate_alpha", !alpha_out->links.empty() && unassociate_alpha);
  compiler.parameter("is_float", is_float);
  compiler.parameter("is_tiled", is_tiled);
  compiler.parameter(this, "interpolation");
  compiler.parameter(this, "extension");

  compiler.add(this, "node_image_texture");
#endif
}

void SamplerTextureNode::cull_tiles(Scene *scene, ShaderGraph *graph)
{
#if 0
  if (!scene->params.background) {
    /* During interactive renders, all tiles are loaded.
     * While we could support updating this when UVs change, that could lead
     * to annoying interruptions when loading images while editing UVs. */
    return;
  }

  /* Only check UVs for tile culling when using tiles. */
  if (tiles.size() == 0) {
    return;
  }

  ShaderInput *vector_in = input("Vector");
  ustring attribute;
  if (vector_in->link) {
    ShaderNode *node = vector_in->link->parent;
    if (node->type == UVMapNode::get_node_type()) {
      UVMapNode *uvmap = (UVMapNode *)node;
      attribute = uvmap->get_attribute();
    } else if (node->type == TextureCoordinateNode::get_node_type()) {
      if (vector_in->link != node->output("UV")) {
        return;
      }
    } else {
      return;
    }
  }

  unordered_set<int> used_tiles;
  /* TODO(lukas): This is quite inefficient. A fairly simple improvement would
   * be to have a cache in each mesh that is indexed by attribute.
   * Additionally, building a graph-to-meshes list once could help. */
  for (Geometry *geom : scene->geometry) {
    for (Node *node : geom->get_used_shaders()) {
      Shader *shader = static_cast<Shader *>(node);
      if (shader->graph.get() == graph) {
        geom->get_uv_tiles(attribute, used_tiles);
      }
    }
  }

  array<int> new_tiles;
  for (const int tile : tiles) {
    if (used_tiles.count(tile)) {
      new_tiles.push_back_slow(tile);
    }
  }
  tiles.steal_data(new_tiles);
#endif
}

CCL_NAMESPACE_END
