// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Object.h"
// std
#include <atomic>
#include <cstdarg>

namespace cycles {

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
  // no-op
}

bool Object::getProperty(
    const std::string_view &name, ANARIDataType type, void *ptr, uint32_t flags)
{
  if (name == "valid" && type == ANARI_BOOL) {
    helium::writeToVoidP(ptr, isValid());
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

void Object::markCommitted()
{
  helium::BaseObject::markCommitted();
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

} // namespace cycles

CYCLES_ANARI_TYPEFOR_DEFINITION(cycles::Object *);
