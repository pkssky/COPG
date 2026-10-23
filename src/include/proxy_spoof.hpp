// COPG open-source engine — per-app proxy spoofing hooks (:proxy).
//
// Arms the SOCKS5 endpoint from PROXY/manifest.json (see proxy.hpp for the
// table format) and redirects the app's outgoing TCP connections through it.
// Interface mirrors spoof/cpu.cpp and spoof/gpu.cpp: set_* returns false when
// the profile is missing (caller then skips the hook).
#pragma once

#include <string>

#include "zygisk.hpp"

namespace copg {
namespace proxy {

// Arm the endpoint for `key` ("default" or an explicit PROXY/manifest.json
// key). Returns false when the key is absent or the host does not resolve.
bool set_endpoint(const std::string &key);

// Register the PLT hooks (call before zygisk::Api::pltHookCommit()).
void register_hooks(zygisk::Api *api);

}  // namespace proxy
}  // namespace copg
