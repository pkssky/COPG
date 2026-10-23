// COPG open-source engine — GPU vendor/renderer spoofing.
#include "gpu_spoof.hpp"
#include "gpu.hpp"
#include "log.hpp"
#include "paths.hpp"

#include <cstring>
#include <string>

// GL ES enum values (from GLES2/gl.h.h) — no GL headers needed.
#define COPG_GL_VENDOR   0x1F00
#define COPG_GL_RENDERER 0x1F01

namespace copg {
namespace gpu {
namespace {

using glGetString_fn = const unsigned char *(*)(unsigned int name);

glGetString_fn g_real_glGetString = nullptr;

// Armed strings ("" = not armed).
std::string g_vendor;
std::string g_renderer;

const unsigned char *hook_glGetString(unsigned int name) {
    if (!g_renderer.empty() && name == COPG_GL_RENDERER) {
        COPG_LOGD("gpu: GL_RENDERER -> %s", g_renderer.c_str());
        return reinterpret_cast<const unsigned char *>(g_renderer.c_str());
    }
    if (!g_vendor.empty() && name == COPG_GL_VENDOR) {
        return reinterpret_cast<const unsigned char *>(g_vendor.c_str());
    }
    return g_real_glGetString(name);
}

}  // namespace

bool set_key(const std::string &key) {
    const GpuEntry *e = gpu_lookup(key, kGpuManifest);
    if (!e) {
        COPG_LOGW("gpu: manifest has no key '%s'", key.c_str());
        return false;
    }
    g_vendor = e->vendor;
    g_renderer = e->renderer;
    COPG_LOGI("gpu: armed '%s' (%s / %s)", key.c_str(), g_vendor.c_str(),
              g_renderer.c_str());
    return true;
}

void register_hooks(zygisk::Api *api) {
    if (g_renderer.empty()) return;
    // libGLESv2 covers GLES 2/3 contexts; libGLESv1_cm covers GLES 1.
    const char *kLibs[] = {"libGLESv2.so", "libGLESv1_cm.so"};
    for (const char *lib : kLibs) {
        api->pltHookRegister(lib, "glGetString",
                             reinterpret_cast<void *>(hook_glGetString),
                             reinterpret_cast<void **>(&g_real_glGetString));
    }
}

}  // namespace gpu
}  // namespace copg
