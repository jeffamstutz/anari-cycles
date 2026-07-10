/*
 * Copyright (c) 2019-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "Device.h"
#include "anari/backend/LibraryImpl.h"
#include "anari_library_cycles_export.h"
// cycles
#include "util/log.h"
#include "util/path.h"
// std
#include <cstdlib>
#include <mutex>
#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace anari_cycles {

const char **query_extensions();

// Point Cycles' runtime path lookup (ccl::path_get) at a root next to this
// plugin instead of its default — the directory of the host *executable*,
// which the plugin cannot control. Precompiled GPU kernels are installed at
// <plugin dir>/cycles/lib/kernel_*.zst (see device/CMakeLists.txt), so with
// root = <plugin dir>/cycles the kernels resolve without any runtime
// dependency on the Cycles kernel source tree, nvcc, or the OptiX SDK.
static void initCyclesRuntime()
{
  static std::once_flag onceFlag;
  std::call_once(onceFlag, []() {
    // Cycles logs (e.g. kernel resolution: "Using precompiled kernel") go to
    // stdout/stderr, gated at 'info important' by default. Allow turning them
    // up without code changes.
    if (const char *level = std::getenv("ANARI_CYCLES_LOG_LEVEL"))
      ccl::log_level_set(ccl::string(level));

#ifndef _WIN32
    Dl_info info;
    if (dladdr(reinterpret_cast<void *>(&initCyclesRuntime), &info)
        && info.dli_fname && info.dli_fname[0] != '\0') {
      const ccl::string pluginDir = ccl::path_dirname(ccl::string(info.dli_fname));
      ccl::path_init(ccl::path_join(pluginDir, "cycles"), "");
    }
#else
    // TODO(Windows): resolve the module path via GetModuleHandleExA(
    // GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) + GetModuleFileNameA and call
    // ccl::path_init() the same way.
#endif
  });
}

struct CyclesLibrary : public anari::LibraryImpl {
  CyclesLibrary(void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr);

  ANARIDevice newDevice(const char *subtype) override;
  const char **getDeviceExtensions(const char *deviceType) override;
};

// Definitions ////////////////////////////////////////////////////////////////

CyclesLibrary::CyclesLibrary(void *lib,
                             ANARIStatusCallback defaultStatusCB,
                             const void *statusCBPtr)
    : anari::LibraryImpl(lib, defaultStatusCB, statusCBPtr)
{
  initCyclesRuntime();
}

ANARIDevice CyclesLibrary::newDevice(const char * /*subtype*/)
{
  return (ANARIDevice) new CyclesDevice(this_library());
}

const char **CyclesLibrary::getDeviceExtensions(const char * /*deviceType*/)
{
  return query_extensions();
}

}  // namespace anari_cycles

// Define library entrypoint //////////////////////////////////////////////////

extern "C" CYCLES_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_ENTRYPOINT(cycles, handle, scb, scbPtr)
{
  return (ANARILibrary) new anari_cycles::CyclesLibrary(handle, scb, scbPtr);
}

extern "C" CYCLES_DEVICE_INTERFACE ANARIDevice
anariNewCyclesDevice(ANARIStatusCallback defaultCallback, const void *userPtr)
{
  anari_cycles::initCyclesRuntime();
  return (ANARIDevice) new anari_cycles::CyclesDevice(defaultCallback, userPtr);
}
