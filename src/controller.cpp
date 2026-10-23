// COPG open-source engine — root-side controller daemon.
//
// All spoofing happens inside the Zygisk module (per app process); the
// controller only owns shared runtime state, repairs file metadata and
// validates the config:
//
//   controller            run in the foreground (service.sh backgrounds it)
//   controller perms      re-assert modes/SELinux contexts on config files
//                         (the WebUI rewrites COPG.json via ksu.exec)
//   controller status     print state dir + config summary + validation
//                         (missing cpu/gpu/proxy/sensor profiles,
//                          unimplemented tags in use)
//
// It deliberately does NOT touch system properties or mounts anything.
#include "config.hpp"
#include "gpu.hpp"
#include "log.hpp"
#include "paths.hpp"
#include "proxy.hpp"
#include "sensor.hpp"
#include "strings.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <string>
#include <vector>

namespace {

volatile sig_atomic_t g_stop = 0;

void on_signal(int) { g_stop = 1; }

std::string pid_file_path() { return std::string(copg::kStateDir) + "/controller.pid"; }

bool ensure_state_dir() {
    if (mkdir(copg::kStateDir, 0700) != 0 && errno != EEXIST) {
        COPG_LOGE("cannot create %s: %s", copg::kStateDir, strerror(errno));
        return false;
    }
    return true;
}

bool write_pid_file() {
    FILE *f = fopen(pid_file_path().c_str(), "w");
    if (!f) return false;
    fprintf(f, "%d\n", getpid());
    fclose(f);
    return true;
}

// Best-effort chmod + chcon (chcon is a no-op without a matching policy).
// `path` is only ever built from kCpuDir + a validated directory entry name
// (see cmd_perms), so it cannot contain shell metacharacters.
void set_file_meta(const std::string &path, mode_t mode) {
    chmod(path.c_str(), mode);
    std::string cmd = "chcon u:object_r:system_file:s0 " + path + " 2>/dev/null";
    int rc = system(cmd.c_str());
    (void) rc;
}

// Strict allow-list for directory entry names: cpuinfo profiles are installed
// from the module ZIP, but a stray file must never be able to smuggle shell
// metacharacters into set_file_meta()'s system() call.
bool safe_entry_name(const std::string &name) {
    if (name.empty() || name.size() > 128) return false;
    for (unsigned char c : name) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

int cmd_perms() {
    int fixed = 0;
    struct stat st{};

    const char *files[][2] = {
        {copg::kConfigPath, "644"},
        {copg::kListPath, "644"},
        {copg::kGpuManifest, "644"},
        {copg::kProxyManifest, "644"},
        {copg::kSensorManifest, "644"},
    };
    for (const auto &f : files) {
        if (stat(f[0], &st) == 0) {
            set_file_meta(f[0], static_cast<mode_t>(strtol(f[1], nullptr, 8)));
            ++fixed;
        }
    }

    // CPU profiles are read by apps through the redirect hook; they must look
    // like ordinary world-readable system files (see customize.sh). Walk the
    // directory directly — no shell round-trip, no quoting surprises.
    DIR *d = opendir(copg::kCpuDir);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != nullptr) {
            std::string name = de->d_name;
            if (name.rfind("cpuinfo_", 0) != 0) continue;
            if (!safe_entry_name(name)) {
                COPG_LOGW("perms: skipping suspicious file name: %s", name.c_str());
                continue;
            }
            set_file_meta(std::string(copg::kCpuDir) + "/" + name, 0444);
            ++fixed;
        }
        closedir(d);
    }

    printf("perms: %d entries normalized\n", fixed);
    return 0;
}

// Validate the config and print a summary. Returns the number of problems.
int report_config() {
    copg::Config cfg;
    std::string err;
    if (!copg::load_config(copg::kConfigPath, &cfg, &err)) {
        printf("config    : %s\n", err.c_str());
        return 1;
    }
    printf("config    : %s\n", copg::kConfigPath);

    size_t packages = 0, cpu_n = 0, gpu_n = 0, aid_n = 0, serial_n = 0, tz_n = 0;
    size_t cow_n = 0, blocked_n = 0, proxy_n = 0, sensor_n = 0, uptime_n = 0;
    size_t vpn_n = 0, hidedev_n = 0;
    std::vector<std::string> bad_cpu, bad_gpu, bad_proxy, bad_sensor, unsupported;

    for (const auto &key : cfg.profile_order) {
        auto pit = cfg.profiles.find(key);
        if (pit == cfg.profiles.end()) continue;
        for (const auto &e : pit->second) {
            ++packages;
            if (e.tags.blocked) ++blocked_n;
            if (e.tags.cow) ++cow_n;
            if (e.tags.aid) ++aid_n;
            if (e.tags.serial) ++serial_n;
            if (!e.tags.tz.empty()) ++tz_n;
            if (e.tags.vpn) ++vpn_n;
            if (e.tags.hidedev) ++hidedev_n;
            if (e.tags.uptime_sec > 0) ++uptime_n;

            if (!e.tags.cpu_key.empty()) {
                ++cpu_n;
                std::string p = std::string(copg::kCpuDir) + "/cpuinfo_" + e.tags.cpu_key;
                struct stat st{};
                if (stat(p.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
                    bad_cpu.push_back(e.package + " [" + key + ":cpu=" + e.tags.cpu_key + "]");
            }
            if (!e.tags.gpu_key.empty()) {
                ++gpu_n;
                if (!copg::gpu_lookup(e.tags.gpu_key, copg::kGpuManifest))
                    bad_gpu.push_back(e.package + " [" + key + ":gpu=" + e.tags.gpu_key + "]");
            }
            if (!e.tags.proxy_key.empty()) {
                ++proxy_n;
                if (!copg::proxy_lookup(e.tags.proxy_key, copg::kProxyManifest))
                    bad_proxy.push_back(e.package + " [" + key + ":proxy=" + e.tags.proxy_key + "]");
            }
            if (!e.tags.sensor_key.empty()) {
                ++sensor_n;
                if (!copg::sensor_lookup(e.tags.sensor_key, copg::kSensorManifest))
                    bad_sensor.push_back(e.package + " [" + key + ":sensor=" + e.tags.sensor_key + "]");
            }
            for (const auto &u : e.tags.unsupported) {
                std::string line = e.package + " [" + key + ":" + u + "]";
                if (std::find(unsupported.begin(), unsupported.end(), line) == unsupported.end())
                    unsupported.push_back(line);
            }
        }
    }

    printf("profiles  : %zu (%zu package entries)\n", cfg.profiles.size(), packages);
    printf("devices   : %zu\n", cfg.devices.size());
    printf("order     : ");
    for (size_t i = 0; i < cfg.profile_order.size(); ++i)
        printf("%s%s", i ? ", " : "", cfg.profile_order[i].c_str());
    printf("\n");
    printf("cpu_only  : %zu   blacklist: %zu\n", cfg.cpu_only_packages.size(),
           cfg.cpu_blacklist.size());
    printf("tags      : cpu=%zu gpu=%zu aid=%zu serial=%zu tz=%zu cow=%zu blocked=%zu\n",
           cpu_n, gpu_n, aid_n, serial_n, tz_n, cow_n, blocked_n);
    printf("pro tags  : proxy=%zu sensor=%zu uptime=%zu vpn=%zu hidedev=%zu\n",
           proxy_n, sensor_n, uptime_n, vpn_n, hidedev_n);

    int problems = 0;
    if (!bad_cpu.empty()) {
        problems += static_cast<int>(bad_cpu.size());
        printf("!! missing cpu profiles (%zu):\n", bad_cpu.size());
        for (const auto &b : bad_cpu) printf("     %s\n", b.c_str());
    }
    if (!bad_gpu.empty()) {
        problems += static_cast<int>(bad_gpu.size());
        printf("!! missing gpu keys (%zu):\n", bad_gpu.size());
        for (const auto &b : bad_gpu) printf("     %s\n", b.c_str());
    }
    if (!bad_proxy.empty()) {
        problems += static_cast<int>(bad_proxy.size());
        printf("!! missing proxy keys (%zu):\n", bad_proxy.size());
        for (const auto &b : bad_proxy) printf("     %s\n", b.c_str());
    }
    if (!bad_sensor.empty()) {
        problems += static_cast<int>(bad_sensor.size());
        printf("!! missing sensor keys (%zu):\n", bad_sensor.size());
        for (const auto &b : bad_sensor) printf("     %s\n", b.c_str());
    }
    if (!unsupported.empty()) {
        printf(".. tags not implemented by the open-source engine (%zu):\n", unsupported.size());
        for (const auto &b : unsupported) printf("     %s\n", b.c_str());
    }
    return problems;
}

int cmd_status() {
    printf("state dir : %s\n", copg::kStateDir);
    int problems = report_config();
    printf("verdict   : %s\n", problems ? "CONFIG HAS PROBLEMS" : "config OK");
    return problems ? 1 : 0;
}

void run_daemon() {
    if (!ensure_state_dir()) return;
    write_pid_file();

    struct sigaction sa{};
    sa.sa_handler = on_signal;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    COPG_LOGI("controller started (pid %d)", getpid());
    printf("COPG controller running (pid %d)\n", getpid());
    fflush(stdout);

    while (!g_stop) sleep(5);

    COPG_LOGI("controller stopped");
    unlink(pid_file_path().c_str());
}

}  // namespace

int main(int argc, char **argv) {
    std::string cmd = argc > 1 ? argv[1] : "run";
    if (cmd == "perms") return cmd_perms();
    if (cmd == "status" || cmd == "check") return cmd_status();
    if (cmd == "run") {
        run_daemon();
        return 0;
    }
    fprintf(stderr, "usage: %s [run|perms|status|check]\n", argv[0]);
    return 2;
}
