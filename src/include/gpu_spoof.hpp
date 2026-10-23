// COPG open-source engine — GPU vendor/renderer spoofing.
//
// Games identify the GPU through glGetString(GL_VENDOR / GL_RENDERER).
// We PLT-hook glGetString in the GL ES driver libraries and return the
// configured strings from GPU/manifest.json. Everything else (version,
// extensions, real driver behaviour) is untouched, so rendering keeps
// working normally — only the advertised identity changes.
#pragma once

#include <string>

#include "zygisk.hpp"

namespace copg {
namespace gpu {

// Arm the spoof for manifest key (e.g. "adreno830"). Returns false when the
// key is not present in GPU/manifest.json.
bool set_key(const std::string &key);

// Register the PLT hooks (call before zygisk::Api::pltHookCommit()).
void register_hooks(zygisk::Api *api);

}  // namespace gpu
}  // namespace copg
