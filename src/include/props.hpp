// COPG open-source engine — system property spoofing (Build.* fields).
//
// Strategy: PLT-hook the two bionic entry points that every property reader
// funnels through, and substitute values for the configured Build fields:
//
//   __system_property_read_callback(pi, cb, cookie)
//       We wrap the caller's callback: the real implementation invokes
//       cb(cookie, name, value, serial) — our wrapper sees the property NAME,
//       swaps in the spoofed value when configured, then forwards to the
//       caller's original callback. Covers libbase's GetProperty (used by
//       Java's SystemProperties.get, Build.* static init, etc.).
//
//   __system_property_get(name, value)
//       NDK readers; the name is visible directly.
//
// Hooks are process-local by design: they exist only inside the target app
// process and vanish when it dies. No global prop is ever touched, so the
// "cow" tag (copy-on-write, restore after app exit) is satisfied for free.
//
// The lookup table is an unordered_map: __system_property_get /
// read_callback fire on every property read in the process, so the hot path
// must be O(1), not O(log n) over a std::map.
#pragma once

#include <string>

#include "config.hpp"  // DeviceProfile
#include "zygisk.hpp"

namespace copg {
namespace props {

// Arm the spoof table.
//   device     — BRAND/MODEL/... (nullptr = none)
//   android_id — per-app derived id for the :aid tag (empty = not used)
//   serial     — per-app derived serial for the :serial tag (empty = not used;
//                a static SERIAL field inside `device` takes precedence and is
//                already covered by the field map)
//   timezone   — zone id for the :tz=<zone> tag (empty = not used)
void set_state(const DeviceProfile *device, const std::string &android_id,
               const std::string &serial, const std::string &timezone);

// Register the PLT hooks (call before zygisk::Api::pltHookCommit()).
void register_hooks(zygisk::Api *api);

// Lookup used by the hooks and by unit tests: spoofed value for `name`, or
// nullptr when the property is not spoofed.
const char *lookup(const char *name);

}  // namespace props
}  // namespace copg
