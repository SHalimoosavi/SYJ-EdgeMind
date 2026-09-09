#include "dashboard/dashboard.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

namespace {

void usage(const char* argv0) {
    std::printf(
        "SYJ EdgeMind — Local Operations Dashboard\n\n"
        "Usage: %s [--port <port>] [--registry-path <path>]\n\n"
        "  --port <port>           Local TCP port (default: 8787)\n"
        "  --registry-path <path> Model registry path\n"
        "  -h, --help              Show this help\n",
        argv0);
}

bool parse_port(const char* value, int* out) {
    char* end = nullptr;
    const long p = std::strtol(value, &end, 10);
    if (end == value || *end != '\0' || p < 1024 || p > 65535) return false;
    *out = static_cast<int>(p);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    syj::edgemind::dashboard::DashboardConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
            return 0;
        }
        if (arg == "--port") {
            if (++i >= argc || !parse_port(argv[i], &config.port)) {
                std::fprintf(stderr, "ERROR: --port expects an integer in 1024..65535.\n");
                return 2;
            }
            continue;
        }
        if (arg == "--registry-path") {
            if (++i >= argc) {
                std::fprintf(stderr, "ERROR: --registry-path requires a value.\n");
                return 2;
            }
            config.model_registry_path = argv[i];
            continue;
        }
        std::fprintf(stderr, "ERROR: unknown argument: %s\n", arg.c_str());
        usage(argv[0]);
        return 2;
    }

    if (!syj::edgemind::dashboard::generate_dashboard_token(&config.token)) {
        std::fprintf(stderr, "ERROR: unable to obtain secure OS entropy for dashboard bearer token.\n");
        return 1;
    }

    syj::edgemind::dashboard::DashboardServer dashboard(std::move(config));
    if (!dashboard.start()) {
        std::fprintf(stderr, "ERROR: failed to bind dashboard to 127.0.0.1:%d.\n", dashboard.port());
        return 1;
    }

    std::printf("SYJ EdgeMind dashboard: http://127.0.0.1:%d/\n", dashboard.port());
    std::printf("Bearer token (keep private): %s\n", dashboard.token().c_str());
    std::fflush(stdout);
    dashboard.run();
    return 0;
}
