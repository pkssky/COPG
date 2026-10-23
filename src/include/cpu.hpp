// COPG open-source engine — /proc/cpuinfo spoofing.
//
// The upstream module bind-mounts CPU/cpuinfo_<key> over /proc/cpuinfo from a
// root daemon (per-app mount namespaces make that fragile). This engine takes
// the simpler, root-free route: PLT-hook the file-opening entry points and
// redirect reads of /proc/cpuinfo to the profile file inside the module dir.
// The hook lives in the app process only, so nothing leaks to other apps.
#pragma once

#include <string>

#include "zygisk.hpp"

namespace copg {
namespace cpu {

// Arm the redirect for profile `key` (loads CPU/cpuinfo_<key> content).
// Returns false when the profile file is missing.
bool set_profile(const std::string &key);

// Register the PLT hooks (call before zygisk::Api::pltHookCommit()).
void register_hooks(zygisk::Api *api);

}  // namespace cpu
}  // namespace copg
