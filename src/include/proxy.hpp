// COPG open-source engine — per-app proxy table (PROXY/manifest.json).
//
//   [ { "key": "default", "host": "127.0.0.1", "port": 1080,
//       "user": "", "pass": "" }, ... ]
//
// The bare ":proxy" tag uses the entry with key "default"; ":proxy=<key>"
// selects a specific one. user/pass enable SOCKS5 username/password auth
// (RFC 1929); leave both empty for no-auth.
#pragma once

#include <string>

namespace copg {

struct ProxyEntry {
    std::string key;
    std::string host;
    std::string user;
    std::string pass;
    int port = 0;
};

// Look up a proxy endpoint by key. Returns nullptr if absent.
// `manifest_path` is PROXY/manifest.json; an empty/missing file yields nullptr.
const ProxyEntry *proxy_lookup(const std::string &key, const std::string &manifest_path);

}  // namespace copg
