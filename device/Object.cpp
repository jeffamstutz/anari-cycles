// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Object.h"
// anari
#include "anari/anari_cpp.hpp"
// std
#include <atomic>
#include <cstdarg>
#include <cstring>

namespace anari_cycles {

// Object definitions /////////////////////////////////////////////////////////

Object::Object(ANARIDataType type, CyclesGlobalState *s)
    : helium::BaseObject(type, s)
{}

void Object::commitParameters()
{
  // no-op
}

void Object::finalize()
{
  notifyChangeObservers();
}

bool Object::getProperty(const std::string_view &name,
    ANARIDataType type,
    void *ptr,
    uint64_t size,
    uint32_t flags)
{
  if (name == "valid" && type == ANARI_BOOL) {
    helium::writeToVoidP(ptr, isValid());
    return true;
  } else if (name == "bounds" && type == ANARI_FLOAT32_BOX3
      && (this->type() == ANARI_WORLD || this->type() == ANARI_GROUP
          || this->type() == ANARI_INSTANCE)) {
    auto b = bounds();
    anari_vec::float3 r[2];
    r[0] = {b.lower.x, b.lower.y, b.lower.z};
    r[1] = {b.upper.x, b.upper.y, b.upper.z};
    std::memcpy(ptr, &r[0], anari::sizeOf(type));
    return true;
  }

  return false;
}

bool Object::isValid() const
{
  return true;
}

void Object::warnIfUnknownObject() const
{
  // no-op
}

box3 Object::bounds() const
{
  return empty_box3();
}

void Object::markFinalized()
{
  helium::BaseObject::markFinalized();
  switch (type()) {
  case ANARI_SURFACE:
  case ANARI_VOLUME:
  case ANARI_GROUP:
  case ANARI_INSTANCE:
  case ANARI_WORLD:
    deviceState()->objectUpdates.lastSceneChange = helium::newTimeStamp();
    break;
  default:
    break;
  }
}

CyclesGlobalState *Object::deviceState() const
{
  return (CyclesGlobalState *)helium::BaseObject::m_state;
}

// UnknownObject definitions //////////////////////////////////////////////////

UnknownObject::UnknownObject(
    ANARIDataType type, std::string_view subtype, CyclesGlobalState *s)
    : Object(type, s), m_subtype(subtype)
{
  reportMessage(ANARI_SEVERITY_WARNING,
      "created unknown %s object of subtype '%s'",
      anari::toString(type),
      m_subtype.c_str());
}

UnknownObject::~UnknownObject() {}

bool UnknownObject::isValid() const
{
  return false;
}

void UnknownObject::warnIfUnknownObject() const
{
  reportMessage(ANARI_SEVERITY_WARNING,
      "encountered unknown %s object of subtype '%s'",
      anari::toString(type()),
      m_subtype.c_str());
}

} // namespace anari_cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(anari_cycles::Object *);
