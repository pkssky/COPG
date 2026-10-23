// COPG open-source engine — root-side controller daemon.
//
// All spoofing happens inside the Zygisk module (per app process); the
// controller only owns shared runtime state and repairs file metadata:
//
//   controller            run in the foreground (service.sh backgrounds it)
//   controller perms      re-assert modes/SELinux contexts on config files
//                         (the WebUI rewrites COPG.json via ksu.exec)
//   controller status     print state dir + config summary
//
// It deliberately does NOT touch system properties or mounts anything.
#include "config.hpp"
#include "log.hpp"
#include "paths.hpp"
#include "strings.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

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
void set_file_meta(const std::string &path, mode_t mode) {
    chmod(path.c_str(), mode);
    std::string cmd = "chcon u:object_r:system_file:s0 " + path + " 2>/dev/null";
    int rc = system(cmd.c_str());
    (void) rc;
}

int cmd_perms() {
    int fixed = 0;
    struct stat st{};

    const char *files[][2] = {
        {copg::kConfigPath, "644"},
        {copg::kListPath, "644"},
    };
    for (const auto &f : files) {
        if (stat(f[0], &st) == 0) {
            set_file_meta(f[0], static_cast<mode_t>(strtol(f[1], nullptr, 8)));
            ++fixed;
        }
    }

    // CPU profiles are read by apps through the redirect hook; they must look
    // like ordinary world-readable system files (see customize.sh).
    std::string glob = std::string(copg::kCpuDir) + "/cpuinfo_*";
    FILE *p = popen(("ls " + glob + " 2>/dev/null").c_str(), "r");
    if (p) {
        char line[512];
        while (fgets(line, sizeof(line), p)) {
            std::string path(line);
            while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) path.pop_back();
            if (path.empty()) continue;
            set_file_meta(path, 0444);
            ++fixed;
        }
        pclose(p);
    }

    printf("perms: %d entries normalized\n", fixed);
    return 0;
}

int cmd_status() {
    copg::Config cfg;
    std::string err;
    bool ok = copg::load_config(copg::kConfigPath, &cfg, &err);

    printf("state dir : %s\n", copg::kStateDir);
    printf("config    : %s\n", ok ? copg::kConfigPath : err.c_str());
    if (ok) {
        size_t packages = 0;
        for (const auto &kv : cfg.profiles) packages += kv.second.size();
        printf("profiles  : %zu (%zu package entries)\n", cfg.profiles.size(), packages);
        printf("devices   : %zu\n", cfg.devices.size());
        printf("order     : ");
        for (size_t i = 0; i < cfg.profile_order.size(); ++i)
            printf("%s%s", i ? ", " : "", cfg.profile_order[i].c_str());
        printf("\n");
    }
    return ok ? 0 : 1;
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
    if (cmd == "status") return cmd_status();
    if (cmd == "run") {
        run_daemon();
        return 0;
    }
    fprintf(stderr, "usage: %s [run|perms|status]\n", argv[0]);
    return 2;
}
