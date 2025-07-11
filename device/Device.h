// Copyright 2025 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/BaseDevice.h"

#include "CyclesGlobalState.h"
#include "Object.h"

#include "anari_library_cycles_queries.h"

namespace anari_cycles {

struct CyclesDevice : public helium::BaseDevice
{
  /////////////////////////////////////////////////////////////////////////////
  // Main interface to accepting API calls
  /////////////////////////////////////////////////////////////////////////////

  // Data Arrays //////////////////////////////////////////////////////////////

  void *mapArray(ANARIArray a) override;

  // API Objects //////////////////////////////////////////////////////////////

  ANARIArray1D newArray1D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1) override;
  ANARIArray2D newArray2D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t numItems2) override;
  ANARIArray3D newArray3D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t numItems2,
      uint64_t numItems3) override;
  ANARICamera newCamera(const char *type) override;
  ANARIFrame newFrame() override;
  ANARIGeometry newGeometry(const char *type) override;
  ANARIGroup newGroup() override;
  ANARIInstance newInstance(const char *type) override;
  ANARILight newLight(const char *type) override;
  ANARIMaterial newMaterial(const char *material_type) override;
  ANARIRenderer newRenderer(const char *type) override;
  ANARISampler newSampler(const char *type) override;
  ANARISpatialField newSpatialField(const char *type) override;
  ANARISurface newSurface() override;
  ANARIVolume newVolume(const char *type) override;
  ANARIWorld newWorld() override;

  // Query functions //////////////////////////////////////////////////////////

  const char **getObjectSubtypes(ANARIDataType objectType) override;
  const void *getObjectInfo(ANARIDataType objectType,
      const char *objectSubtype,
      const char *infoName,
      ANARIDataType infoType) override;
  const void *getParameterInfo(ANARIDataType objectType,
      const char *objectSubtype,
      const char *parameterName,
      ANARIDataType parameterType,
      const char *infoName,
      ANARIDataType infoType) override;

  // Object + Parameter Lifetime Management ///////////////////////////////////

  int getProperty(ANARIObject object,
      const char *name,
      ANARIDataType type,
      void *mem,
      uint64_t size,
      uint32_t mask) override;

  /////////////////////////////////////////////////////////////////////////////
  // Helper/other functions and data members
  /////////////////////////////////////////////////////////////////////////////

  CyclesDevice(ANARIStatusCallback defaultCallback, const void *userPtr);
  CyclesDevice(ANARILibrary);
  ~CyclesDevice() override;

  int deviceGetProperty(const char *name,
      ANARIDataType type,
      void *mem,
      uint64_t size,
      uint32_t mask) override;

 private:
  void initDevice();

  CyclesGlobalState *deviceState() const;

  bool m_initialized{false};
};

} // namespace anari_cycles
