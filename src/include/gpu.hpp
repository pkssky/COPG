// COPG open-source engine — GPU spoof table (GPU/manifest.json).
//
//   [ { "key": "adreno830", "name": "...", "renderer": "Adreno (TM) 830",
//       "vendor": "Qualcomm", "vendor_id": "0x5143" }, ... ]
#pragma once

#include <string>

namespace copg {

struct GpuEntry {
    std::string key;
    std::string name;
    std::string renderer;   // glGetString(GL_RENDERER)
    std::string vendor;     // glGetString(GL_VENDOR)
};

// Look up a GPU profile by key (e.g. "adreno830"). Returns nullptr if absent.
// `manifest_path` is GPU/manifest.json; an empty/missing file yields nullptr.
const GpuEntry *gpu_lookup(const std::string &key, const std::string &manifest_path);

}  // namespace copg
