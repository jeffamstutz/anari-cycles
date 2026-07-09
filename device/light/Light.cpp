// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Light.h"
// subtypes
#include "Directional.h"
#include "HDRI.h"
#include "Point.h"
#include "QuadLight.h"
#include "Ring.h"
#include "Sky.h"
#include "Spot.h"
#include "UnknownLight.h"
#include "array/Array2D.h"
// cycles
#include "kernel/svm/types.h"
#include "scene/background.h"
#include "scene/camera.h"
#include "scene/shader.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "util/math_base.h"
#include "util/transform.h"
#include "util/types_float3.h"
// std
#include <charconv>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

namespace anari_cycles {

Light::Light(CyclesGlobalState *s, ccl::Light *light)
    : Object(ANARI_LIGHT, s),
      m_cyclesLight(light),
      m_intensityDistributionArray(this)
{}

Light::~Light()
{
  // Object release can happen while the render thread reads the scene, and
  // scene->objects may still reference the light node -- defer its deletion.
  CyclesGlobalState::SceneLock sceneLock(*deviceState());
  deviceState()->retireGeometry(m_cyclesLight);
  if (m_cyclesShader) {
    m_cyclesShader->dereference();
    // delete_node(Shader*) only clears the reference count; Cycles never
    // frees shaders before the scene itself is destroyed.
    deviceState()->scene->delete_node(m_cyclesShader);
  }
}

// The unit-emission graph shared by all analytic lights; the light's actual
// color/intensity is applied via ccl::Light::strength on top.
static std::unique_ptr<ccl::ShaderGraph> makeUnitEmissionGraph()
{
  auto graph = std::make_unique<ccl::ShaderGraph>();
  auto *emission = graph->create_node<ccl::EmissionNode>();
  emission->set_color(ccl::one_float3());
  emission->set_strength(1.f);
  graph->connect(
      emission->output("Emission"), graph->output()->input("Surface"));
  return graph;
}

// Append a number in a locale-independent way (snprintf %g honors
// LC_NUMERIC; a comma decimal separator would corrupt the IES stream, whose
// parser treats commas as whitespace).
template <typename T>
static void appendNumber(std::string &s, T value, char sep)
{
  char buf[64];
  auto res = std::to_chars(buf, buf + sizeof(buf) - 1, value);
  *res.ptr = sep;
  s.append(buf, res.ptr + 1);
}

// Serialize an ANARI intensityDistribution as an IES LM-63 Type C photometry
// string for Cycles' IESLightNode (parser: cycles/src/util/ies.cpp).
// Vertical angles are the ANARI polar angles (uniform over [0,180] deg);
// horizontal angles are the C-planes (uniform over [0,360) deg, plus a
// duplicated wrap row at 360 so the kernel interpolates across C0). A 1D
// distribution (nC == 1) emits a single horizontal block, which Cycles
// expands to a rotationally symmetric profile. Expects nV >= 2 and nC >= 1.
static std::string synthesizeTypeCIES(
    const std::vector<float> &values, int nV, int nC)
{
  // Cycles converts IES candela values to watts with a fixed factor
  // (4*pi/177.83, util/ies.cpp); pre-divide via the file's candela
  // multiplier so the kernel returns the raw ANARI modulation weights.
  const double candelaCompensation = 1.0 / 0.0706650768394;

  const int nH = nC <= 1 ? 1 : nC + 1;

  std::string s = "IESNA:LM-63-2002\nTILT=NONE\n";
  // lamps, lumens, candela multiplier, #v-angles, #h-angles, photometric
  // type (1 = C), units, width, length, height, ballast factor,
  // ballast-lamp factor, input watts
  s += "1 -1 ";
  appendNumber(s, candelaCompensation, ' ');
  appendNumber(s, nV, ' ');
  appendNumber(s, nH, ' ');
  s += "1 2 0 0 0 1 1 0\n";
  for (int i = 0; i < nV; i++)
    appendNumber(s, 180.0 * i / (nV - 1), i + 1 == nV ? '\n' : ' ');
  for (int j = 0; j < nH; j++)
    appendNumber(s, 360.0 * j / nC, j + 1 == nH ? '\n' : ' ');
  for (int j = 0; j < nH; j++) {
    const float *row = values.data() + size_t(j % nC) * nV;
    for (int i = 0; i < nV; i++)
      appendNumber(s, row[i], i + 1 == nV ? '\n' : ' ');
  }
  return s;
}

void Light::attachUnitEmissionShader()
{
  auto graph = makeUnitEmissionGraph();

  m_cyclesShader = deviceState()->scene->create_node<ccl::Shader>();
  m_cyclesShader->name = "anari_light_emission";
  m_cyclesShader->set_graph(std::move(graph));
  m_cyclesShader->reference();
  m_cyclesShader->tag_update(deviceState()->scene);

  ccl::array<ccl::Node *> usedShaders;
  usedShaders.push_back_slow(m_cyclesShader);
  m_cyclesLight->set_used_shaders(usedShaders);

  // MIS lets camera rays display the light geometry (KHR_AREA_LIGHTS
  // 'visible', default true; the per-instance camera-visibility switch is
  // in Group.cpp). Cycles' own default is false.
  m_cyclesLight->set_use_mis(true);
}

ccl::float3 Light::scaledColor(float scale) const
{
  return scale * ccl::make_float3(m_color[0], m_color[1], m_color[2]);
}

float Light::photometricRadiance(float area)
{
  // ANARI area-light photometric precedence: 'radiance' wins over
  // 'intensity' (W/sr, divided by the emitting area) over 'power' (W,
  // divided by pi times the area for a Lambertian emitter).
  //
  // LIMITATION: 'area' is computed from the light's LOCAL parameters at
  // commit time, but instance transforms (Group.cpp) are applied later and
  // may scale the emitter. Since the Cycles lights are configured with
  // normalize off (emitted radiance independent of world-space area), only
  // 'radiance' is exact under instance scaling; 'intensity' and 'power' are
  // only exact for unscaled instances. Fixing this would require per-instance
  // light nodes; documented in the device's extension JSON instead.
  float radiance = 1.f;
  if (hasParam("radiance", ANARI_FLOAT32)) {
    radiance = getParam<float>("radiance", 1.f);
  } else if (hasParam("intensity", ANARI_FLOAT32) && area > 0.f) {
    radiance = getParam<float>("intensity", 1.f) / area;
  } else if (hasParam("power", ANARI_FLOAT32) && area > 0.f) {
    radiance = getParam<float>("power", 1.f) / (float(M_PI) * area);
  }
  return std::clamp(radiance, 0.f, std::numeric_limits<float>::max());
}

Light::IntensityDistribution Light::getIntensityDistributionParam()
{
  IntensityDistribution dist;
  if (!hasParam("intensityDistribution")) {
    m_intensityDistributionArray = nullptr;
    return dist;
  }

  const float *data = nullptr;
  size_t nV = 0, nC = 1;
  ANARIDataType elementType = ANARI_UNKNOWN;
  auto a1 = getParamObject<Array1D>("intensityDistribution");
  auto a2 = getParamObject<Array2D>("intensityDistribution");
  m_intensityDistributionArray =
      a1 ? (helium::BaseObject *)a1 : (helium::BaseObject *)a2;
  if (a1) {
    elementType = a1->elementType();
    data = a1->beginAs<float>();
    nV = a1->size();
  } else if (a2) {
    elementType = a2->elementType();
    data = a2->dataAs<float>();
    nV = a2->size().x;
    nC = a2->size().y;
  } else {
    reportMessage(ANARI_SEVERITY_WARNING,
        "light 'intensityDistribution' must be an ARRAY1D or ARRAY2D of "
        "FLOAT32; ignoring");
    return dist;
  }
  if (elementType != ANARI_FLOAT32) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "light 'intensityDistribution' must have FLOAT32 elements "
        "(got %s); ignoring",
        anari::toString(elementType));
    return dist;
  }

  if (nV < 2) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "light 'intensityDistribution' needs at least two polar-angle "
        "samples (got %zu); ignoring",
        nV);
    return dist;
  }

  dist.nV = int(nV);
  dist.nC = int(std::max<size_t>(nC, 1));
  dist.values.resize(nV * dist.nC);
  bool sawInvalid = false;
  for (size_t i = 0; i < dist.values.size(); i++) {
    float v = data[i];
    if (!std::isfinite(v) || v < 0.f) {
      sawInvalid = true;
      v = std::isfinite(v) ? std::max(v, 0.f) : 0.f;
    }
    dist.values[i] = v;
  }
  if (sawInvalid) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "light 'intensityDistribution' has negative or non-finite entries; "
        "clamping to 0");
  }
  return dist;
}

void Light::updateEmissionShaderDistribution(const IntensityDistribution &dist,
    const math::float3 &rowX,
    const math::float3 &rowY,
    const math::float3 &rowZ)
{
  if (!dist.present()) {
    // Keep the constructor-built unit-emission graph untouched (renders
    // without the parameter must be unchanged); only rebuild to drop a
    // previously applied distribution.
    if (!m_shaderHasDistribution)
      return;
    m_cyclesShader->set_graph(makeUnitEmissionGraph());
    m_cyclesShader->tag_update(deviceState()->scene);
    m_shaderHasDistribution = false;
    m_appliedIES.clear();
    return;
  }

  // Skip the rebuild (and the shader recompile + IES table re-upload it
  // triggers) when a re-commit leaves the distribution and orientation
  // unchanged; the IES string is a full fingerprint of the sample values.
  auto iesString = synthesizeTypeCIES(dist.values, dist.nV, dist.nC);
  if (m_shaderHasDistribution && iesString == m_appliedIES
      && rowX == m_appliedRows[0] && rowY == m_appliedRows[1]
      && rowZ == m_appliedRows[2]) {
    return;
  }

  auto graph = std::make_unique<ccl::ShaderGraph>();

  // In light shaders 'Incoming' is the world-space emission direction
  // (light sample point -> receiver); bring it into the per-instance local
  // space of the light...
  auto *geom = graph->create_node<ccl::GeometryNode>();
  auto *toLocal = graph->create_node<ccl::VectorTransformNode>();
  toLocal->set_transform_type(ccl::NODE_VECTOR_TRANSFORM_TYPE_VECTOR);
  toLocal->set_convert_from(ccl::NODE_VECTOR_TRANSFORM_CONVERT_SPACE_WORLD);
  toLocal->set_convert_to(ccl::NODE_VECTOR_TRANSFORM_CONVERT_SPACE_OBJECT);
  graph->connect(geom->output("Incoming"), toLocal->input("Vector"));

  // ...and map it (dot products with the caller's matrix rows, which also
  // undo any skew/scale baked into the light's transform) to the vector
  // whose angles Cycles' IES kernel decodes (see Light.h).
  const math::float3 rows[3] = {rowX, rowY, rowZ};
  ccl::ShaderNode *dots[3];
  for (int i = 0; i < 3; i++) {
    auto *dot = graph->create_node<ccl::VectorMathNode>();
    dot->set_math_type(ccl::NODE_VECTOR_MATH_DOT_PRODUCT);
    dot->set_vector2(ccl::make_float3(rows[i].x, rows[i].y, rows[i].z));
    graph->connect(toLocal->output("Vector"), dot->input("Vector1"));
    dots[i] = dot;
  }
  auto *combine = graph->create_node<ccl::CombineXYZNode>();
  graph->connect(dots[0]->output("Value"), combine->input("X"));
  graph->connect(dots[1]->output("Value"), combine->input("Y"));
  graph->connect(dots[2]->output("Value"), combine->input("Z"));

  auto *ies = graph->create_node<ccl::IESLightNode>();
  ies->set_ies(ccl::ustring(iesString));
  graph->connect(combine->output("Vector"), ies->input("Vector"));

  // The IES factor modulates the unit emission; the resolved photometric
  // strength still comes in via ccl::Light::strength.
  auto *emission = graph->create_node<ccl::EmissionNode>();
  emission->set_color(ccl::one_float3());
  graph->connect(ies->output("Fac"), emission->input("Strength"));
  graph->connect(
      emission->output("Emission"), graph->output()->input("Surface"));

  m_cyclesShader->set_graph(std::move(graph));
  m_cyclesShader->tag_update(deviceState()->scene);
  m_shaderHasDistribution = true;
  m_appliedIES = std::move(iesString);
  m_appliedRows[0] = rowX;
  m_appliedRows[1] = rowY;
  m_appliedRows[2] = rowZ;
}

Light *Light::createInstance(std::string_view type, CyclesGlobalState *s)
{
  if (type == "directional")
    return new Directional(s);
  else if (type == "hdri")
    return new HDRI(s);
  else if (type == "sky")
    return new Sky(s);
  else if (type == "point")
    return new Point(s);
  else if (type == "spot")
    return new Spot(s);
  else if (type == "quad")
    return new QuadLight(s);
  else if (type == "ring")
    return new Ring(s);
  else
    return new UnknownLight(type, s);
}

math::float3 Light::getNormalizedDirection(
    const char *name, const math::float3 &fallback)
{
  const auto dir = getParam<math::float3>(name, fallback);
  const float len = math::length(dir);
  if (!std::isfinite(len) || len <= 0.f) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "light '%s' parameter is zero-length or non-finite; "
        "using (%g, %g, %g)",
        name,
        double(fallback.x),
        double(fallback.y),
        double(fallback.z));
    return fallback;
  }
  return dir / len;
}

void Light::commitParameters()
{
  m_color = getParam<anari_vec::float3>("color", {1.f, 1.f, 1.f});
  // KHR_AREA_LIGHTS: light geometry is visible to camera rays by default.
  m_visible = getParam<bool>("visible", true);
  // CYCLES_LIGHT_LINKING: 'lightSet' restricts illumination to surfaces
  // whose 'receiverLightSet' names the same set; 'shadowSet' restricts
  // shadowing of this light to surfaces whose 'shadowBlockerSet' names the
  // same set. Baked onto this light's per-instance ccl::Objects at
  // world-rebuild time (Group::addGroupToCurrentCyclesScene()).
  auto *state = deviceState();
  m_lightSetMembership =
      getLinkSetMembershipParam("lightSet", state->lightLinkSets);
  m_shadowSetMembership =
      getLinkSetMembershipParam("shadowSet", state->shadowLinkSets);
  // CYCLES_LIGHTGROUPS
  m_lightGroup = getParamString("lightGroup", "");
}

uint64_t Light::getLinkSetMembershipParam(
    const char *name, CyclesGlobalState::LinkSetRegistry &reg)
{
  const std::string setName = getParamString(name, "");
  if (setName.empty())
    return ~uint64_t(0);
  const int idx = reg.resolve(setName);
  if (idx < 0) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'%s' value '%s' ignored -- Cycles supports at most 63 distinct "
        "link-set names per namespace",
        name,
        setName.c_str());
    return ~uint64_t(0);
  }
  return uint64_t(1) << uint64_t(idx);
}

uint64_t Light::lightSetMembership() const
{
  return m_lightSetMembership;
}

uint64_t Light::shadowSetMembership() const
{
  return m_shadowSetMembership;
}

const std::string &Light::lightGroup() const
{
  return m_lightGroup;
}

bool Light::visibleToCamera() const
{
  return m_visible;
}

ccl::Light *Light::secondaryCyclesLight() const
{
  return nullptr;
}

math::mat4 Light::secondaryXfm() const
{
  return math::mat4(linalg::identity);
}

void Light::setCameraBackgroundColor(const math::float3 &)
{
  // only meaningful for HDRI lights
}

void Light::finalize()
{
  // Light state is baked into per-instance ccl::Objects at world-rebuild
  // time (transform, camera visibility, secondary emitters), and neither
  // helium object arrays nor Object::markFinalized() propagate light
  // commits to the world -- invalidate the baked scene objects here so any
  // light change triggers a rebuild on the next frame.
  // TODO: make light updates more efficient (rebuilds the whole world).
  deviceState()->objectUpdates.lastSceneChange = helium::newTimeStamp();
  Object::finalize();
}

ccl::Light *Light::cyclesLight() const
{
  return m_cyclesLight;
}

ccl::Shader *Light::cyclesShader() const
{
  return m_cyclesShader;
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Light *);
