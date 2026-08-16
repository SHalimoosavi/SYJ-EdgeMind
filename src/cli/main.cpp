// SYJ EdgeMind CLI.
//
// Deliberately includes ONLY the public C API header — no llama.h, no
// internal SYJ EdgeMind C++ headers. This is what "platform code doesn't
// know llama.cpp internals" means in practice (Phase 1 spec §15/§25); the
// Windows platform layer (Phase 5) will wrap this same C API, not a
// different one.

#include "api/edge_mind_api.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

namespace {

void print_usage(const char* argv0) {
    std::printf(
        "SYJ EdgeMind — Private, Offline AI for Low-Memory Devices.\n\n"
        "Usage:\n"
        "  %s --model <path.gguf> [options] [\"prompt\"]\n\n"
        "Options:\n"
        "  --model <path>         Path to a local GGUF model file (required)\n"
        "  --context <n>          Context size in tokens (default: 1024)\n"
        "  --threads <n>          CPU threads (default: hardware concurrency, min 1)\n"
        "  --temperature <f>      Sampling temperature (default: 0.7)\n"
        "  --top-p <f>            Nucleus sampling threshold (default: 0.9)\n"
        "  --top-k <n>            Top-k sampling cutoff (default: 40)\n"
        "  --max-tokens <n>       Maximum tokens to generate (default: 256)\n"
        "  --memory-budget <mb>   Memory budget in MB (default: 3000)\n"
        "  --safety-reserve <mb>  Memory safety reserve in MB, held back from the\n"
        "                         budget and never allocated toward (default: 300)\n"
        "  --time-limit-minutes <n>  Session time limit in minutes (default: unlimited)\n"
        "  --message-limit <n>       Messages allowed per reset period (default: unlimited)\n"
        "  --token-limit <n>         Generated tokens allowed per reset period (default: unlimited)\n"
        "  --reset-period-hours <n>  How often message/token limits reset (default: 24)\n"
        "  --usage-state-path <p>    Local file for persisted usage state\n"
        "                            (default: .syj_edgemind_usage_state)\n"
        "  --checksum <sha256>       Expected SHA-256 of the model file (optional;\n"
        "                            GGUF structural verification always runs\n"
        "                            regardless of whether this is set)\n"
        "  --registry-path <p>       Local file for the model registry\n"
        "                            (default: .syj_edgemind_model_registry)\n"
        "  -h, --help             Show this help and exit\n\n"
        "If a prompt is given as a trailing argument, SYJ EdgeMind generates a\n"
        "single response and exits. Otherwise it starts interactive mode:\n"
        "  /help    show interactive commands\n"
        "  /info    show loaded model info (from llama.cpp, after loading)\n"
        "  /verify  show the model-verification report (from SYJ EdgeMind's own\n"
        "           GGUF reader, independent of llama.cpp)\n"
        "  /memory  show the memory-budget diagnostic from the last load\n"
        "  /usage   show current usage, remaining quota, and reset time\n"
        "  /reset   clear the context and start fresh\n"
        "  /quit    exit\n",
        argv0);
}

bool parse_float_arg(const char* s, float* out) {
    if (s == nullptr) return false;
    char* end = nullptr;
    const float v = std::strtof(s, &end);
    if (end == s || *end != '\0') return false;
    *out = v;
    return true;
}

bool parse_int_arg(const char* s, int32_t* out) {
    if (s == nullptr) return false;
    char* end = nullptr;
    const long v = std::strtol(s, &end, 10);
    if (end == s || *end != '\0') return false;
    *out = static_cast<int32_t>(v);
    return true;
}

bool parse_int64_arg(const char* s, int64_t* out) {
    if (s == nullptr) return false;
    char* end = nullptr;
    const long long v = std::strtoll(s, &end, 10);
    if (end == s || *end != '\0') return false;
    *out = static_cast<int64_t>(v);
    return true;
}

int stream_to_stdout(const char* piece, void* /*user_data*/) {
    std::fputs(piece, stdout);
    std::fflush(stdout);
    return 1; // continue
}

void print_model_info(syj_edgemind_runtime* rt) {
    syj_edgemind_model_info info;
    if (syj_edgemind_get_model_info(rt, &info) != 0) {
        std::printf("No model info available.\n");
        return;
    }
    std::printf(
        "Model:         %s\n"
        "Parameters:    %llu\n"
        "Size on disk:  %.1f MB\n"
        "Trained ctx:   %d\n"
        "Active ctx:    %d\n"
        "Threads:       %d\n",
        info.description,
        static_cast<unsigned long long>(info.n_params),
        static_cast<double>(info.model_size_bytes) / (1024.0 * 1024.0),
        info.n_ctx_train,
        info.n_ctx,
        info.n_threads);
}

void print_memory_report(const syj_edgemind_runtime* rt) {
    const size_t needed = syj_edgemind_get_memory_report(rt, nullptr, 0);
    if (needed == 0) {
        std::printf("No memory report available.\n");
        return;
    }
    std::string buf(needed + 1, '\0');
    syj_edgemind_get_memory_report(rt, buf.data(), buf.size());
    std::printf("%s\n", buf.c_str());
}

void print_usage_report(const syj_edgemind_runtime* rt) {
    const size_t needed = syj_edgemind_get_usage_report(rt, nullptr, 0);
    if (needed == 0) {
        std::printf("No usage report available.\n");
        return;
    }
    std::string buf(needed + 1, '\0');
    syj_edgemind_get_usage_report(rt, buf.data(), buf.size());
    std::printf("%s\n", buf.c_str());
}

void print_verification_report(const syj_edgemind_runtime* rt) {
    const size_t needed = syj_edgemind_get_verification_report(rt, nullptr, 0);
    if (needed == 0) {
        std::printf("No verification report available.\n");
        return;
    }
    std::string buf(needed + 1, '\0');
    syj_edgemind_get_verification_report(rt, buf.data(), buf.size());
    std::printf("%s\n", buf.c_str());
}

} // namespace

int main(int argc, char** argv) {
    syj_edgemind_config config;
    syj_edgemind_default_config(&config);

    std::string model_path;
    std::string trailing_prompt;
    bool have_trailing_prompt = false;

    unsigned hw_threads = std::thread::hardware_concurrency();
    if (hw_threads > 0) {
        config.threads = static_cast<int32_t>(hw_threads);
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        auto next = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "ERROR: %s requires a value.\n", flag);
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--model") {
            model_path = next("--model");
        } else if (arg == "--context") {
            if (!parse_int_arg(next("--context"), &config.context_size)) {
                std::fprintf(stderr, "ERROR: --context expects an integer.\n");
                return 2;
            }
        } else if (arg == "--threads") {
            if (!parse_int_arg(next("--threads"), &config.threads)) {
                std::fprintf(stderr, "ERROR: --threads expects an integer.\n");
                return 2;
            }
        } else if (arg == "--temperature") {
            if (!parse_float_arg(next("--temperature"), &config.temperature)) {
                std::fprintf(stderr, "ERROR: --temperature expects a float.\n");
                return 2;
            }
        } else if (arg == "--top-p") {
            if (!parse_float_arg(next("--top-p"), &config.top_p)) {
                std::fprintf(stderr, "ERROR: --top-p expects a float.\n");
                return 2;
            }
        } else if (arg == "--top-k") {
            if (!parse_int_arg(next("--top-k"), &config.top_k)) {
                std::fprintf(stderr, "ERROR: --top-k expects an integer.\n");
                return 2;
            }
        } else if (arg == "--max-tokens") {
            if (!parse_int_arg(next("--max-tokens"), &config.max_tokens)) {
                std::fprintf(stderr, "ERROR: --max-tokens expects an integer.\n");
                return 2;
            }
        } else if (arg == "--memory-budget") {
            if (!parse_int64_arg(next("--memory-budget"), &config.memory_budget_mb)) {
                std::fprintf(stderr, "ERROR: --memory-budget expects an integer (MB).\n");
                return 2;
            }
        } else if (arg == "--safety-reserve") {
            if (!parse_int64_arg(next("--safety-reserve"), &config.safety_reserve_mb)) {
                std::fprintf(stderr, "ERROR: --safety-reserve expects an integer (MB).\n");
                return 2;
            }
        } else if (arg == "--time-limit-minutes") {
            int64_t minutes = 0;
            if (!parse_int64_arg(next("--time-limit-minutes"), &minutes)) {
                std::fprintf(stderr, "ERROR: --time-limit-minutes expects an integer.\n");
                return 2;
            }
            config.session_time_limit_seconds = minutes * 60;
        } else if (arg == "--message-limit") {
            if (!parse_int64_arg(next("--message-limit"), &config.daily_message_limit)) {
                std::fprintf(stderr, "ERROR: --message-limit expects an integer.\n");
                return 2;
            }
        } else if (arg == "--token-limit") {
            if (!parse_int64_arg(next("--token-limit"), &config.daily_token_limit)) {
                std::fprintf(stderr, "ERROR: --token-limit expects an integer.\n");
                return 2;
            }
        } else if (arg == "--reset-period-hours") {
            int64_t hours = 0;
            if (!parse_int64_arg(next("--reset-period-hours"), &hours)) {
                std::fprintf(stderr, "ERROR: --reset-period-hours expects an integer.\n");
                return 2;
            }
            config.reset_period_seconds = hours * 3600;
        } else if (arg == "--usage-state-path") {
            config.usage_state_path = next("--usage-state-path");
        } else if (arg == "--checksum") {
            config.expected_model_checksum_sha256 = next("--checksum");
        } else if (arg == "--registry-path") {
            config.model_registry_path = next("--registry-path");
        } else if (!arg.empty() && arg[0] != '-') {
            trailing_prompt = arg;
            have_trailing_prompt = true;
        } else {
            std::fprintf(stderr, "ERROR: Unrecognized argument: %s\n", arg.c_str());
            print_usage(argv[0]);
            return 2;
        }
    }

    if (model_path.empty()) {
        std::fprintf(stderr, "ERROR: --model is required.\n\n");
        print_usage(argv[0]);
        return 2;
    }
    config.model_path = model_path.c_str();

    std::printf("SYJ EdgeMind\nOffline Local AI\nContext: %d   Threads: %d   Memory budget: %lld MB (reserve %lld MB)\n",
                config.context_size, config.threads,
                static_cast<long long>(config.memory_budget_mb),
                static_cast<long long>(config.safety_reserve_mb));
    if (config.session_time_limit_seconds > 0 || config.daily_message_limit > 0 || config.daily_token_limit > 0) {
        std::printf("Usage limits: ");
        if (config.session_time_limit_seconds > 0) {
            std::printf("session=%llds ", static_cast<long long>(config.session_time_limit_seconds));
        }
        if (config.daily_message_limit > 0) {
            std::printf("messages=%lld/%llds ", static_cast<long long>(config.daily_message_limit),
                        static_cast<long long>(config.reset_period_seconds));
        }
        if (config.daily_token_limit > 0) {
            std::printf("tokens=%lld/%llds ", static_cast<long long>(config.daily_token_limit),
                        static_cast<long long>(config.reset_period_seconds));
        }
        std::printf("\n");
    }
    std::printf("\n");
    std::printf("Verifying and loading model: %s ...\n", model_path.c_str());

    syj_edgemind_status status = SYJ_EDGEMIND_OK;
    syj_edgemind_runtime* rt = syj_edgemind_create(&config, &status);

    if (status == SYJ_EDGEMIND_ERROR_MODEL_VERIFICATION_FAILED) {
        std::fprintf(stderr, "ERROR: %s\n\n", syj_edgemind_status_message(status));
        print_verification_report(rt);
        syj_edgemind_destroy(rt); // kept alive by create() specifically to allow this
        return 1;
    }

    if (status == SYJ_EDGEMIND_ERROR_MEMORY_BUDGET_EXCEEDED) {
        std::fprintf(stderr, "ERROR: %s\n\n", syj_edgemind_status_message(status));
        print_memory_report(rt);
        syj_edgemind_destroy(rt); // kept alive by create() specifically to allow this
        return 1;
    }

    if (status == SYJ_EDGEMIND_ERROR_QUOTA_EXCEEDED) {
        std::fprintf(stderr, "ERROR: %s\n\n", syj_edgemind_status_message(status));
        print_usage_report(rt);
        syj_edgemind_destroy(rt); // kept alive by create() specifically to allow this
        return 1;
    }

    if (rt == nullptr) {
        std::fprintf(stderr, "ERROR: %s\n", syj_edgemind_status_message(status));
        if (status == SYJ_EDGEMIND_ERROR_MODEL_NOT_FOUND) {
            std::fprintf(stderr, "  %s\n", model_path.c_str());
        }
        return 1;
    }
    std::printf("Model loaded.\n\n");

    if (have_trailing_prompt) {
        std::printf("You:\n%s\n\nSYJ EdgeMind:\n", trailing_prompt.c_str());
        const syj_edgemind_status gen_status =
            syj_edgemind_generate(rt, trailing_prompt.c_str(), stream_to_stdout, nullptr);
        std::printf("\n");
        if (gen_status == SYJ_EDGEMIND_ERROR_QUOTA_EXCEEDED) {
            std::fprintf(stderr, "ERROR: %s\n\n", syj_edgemind_status_message(gen_status));
            print_usage_report(rt);
            syj_edgemind_destroy(rt);
            return 1;
        }
        if (gen_status != SYJ_EDGEMIND_OK) {
            std::fprintf(stderr, "ERROR: %s\n", syj_edgemind_status_message(gen_status));
            syj_edgemind_destroy(rt);
            return 1;
        }
        syj_edgemind_destroy(rt);
        return 0;
    }

    // Interactive mode.
    std::string line;
    while (true) {
        std::printf("You:\n");
        if (!std::getline(std::cin, line)) {
            break; // EOF / user interruption
        }

        if (line == "/quit") {
            break;
        } else if (line == "/help") {
            std::printf("Commands: /help  /info  /verify  /memory  /usage  /reset  /quit\n");
            continue;
        } else if (line == "/info") {
            print_model_info(rt);
            continue;
        } else if (line == "/verify") {
            print_verification_report(rt);
            continue;
        } else if (line == "/memory") {
            print_memory_report(rt);
            continue;
        } else if (line == "/usage") {
            print_usage_report(rt);
            continue;
        } else if (line == "/reset") {
            syj_edgemind_reset(rt);
            std::printf("Context reset.\n");
            continue;
        } else if (line.empty()) {
            continue;
        }

        std::printf("\nSYJ EdgeMind:\n");
        const syj_edgemind_status gen_status = syj_edgemind_generate(rt, line.c_str(), stream_to_stdout, nullptr);
        std::printf("\n\n");
        if (gen_status == SYJ_EDGEMIND_ERROR_QUOTA_EXCEEDED) {
            std::fprintf(stderr, "ERROR: %s\n\n", syj_edgemind_status_message(gen_status));
            print_usage_report(rt);
            // Deliberately continue, not break: a quota denial means "not
            // right now", not "this runtime is broken" — /usage, /info,
            // /memory, /reset all remain usable, unlike a genuine
            // EngineError below.
            continue;
        }
        if (gen_status != SYJ_EDGEMIND_OK) {
            std::fprintf(stderr, "ERROR: %s\n", syj_edgemind_status_message(gen_status));
            // Per Phase 1 spec §14: do not continue with an invalid runtime.
            break;
        }
    }

    syj_edgemind_destroy(rt);
    return 0;
}
