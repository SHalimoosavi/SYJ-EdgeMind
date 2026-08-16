#ifndef SYJ_EDGEMIND_CORE_CONFIG_H
#define SYJ_EDGEMIND_CORE_CONFIG_H

#include <string>

namespace syj::edgemind {

// Conservative context profiles, per docs/memory-model.md.
// Phase 1 only *offers* these as defaults/validation bounds — the formal
// memory-budget engine that measures and enforces real RAM usage against
// them is Phase 2.
enum class ContextProfile {
    ULTRA_LOW = 512,
    LOW       = 1024,
    BALANCED  = 2048,
    ADVANCED  = 4096,
};

// Runtime configuration. Populated from CLI arguments in Phase 1; a file-based
// config (per Phase 0's docs/development.md notes) is not implemented yet.
struct RuntimeConfig {
    std::string model_path;

    // Context size in tokens. Defaults to LOW (1024) per the Phase 1 spec's
    // conservative default. Phase 1 enforces only a sane upper bound
    // (SYJ_EDGEMIND_MAX_CONTEXT below) — it does NOT estimate whether this
    // context actually fits in a given memory budget; that is Phase 2.
    int context_size = static_cast<int>(ContextProfile::LOW);

    // 0 means "let llama.cpp/the OS decide" is NOT used here — SYJ EdgeMind
    // requires an explicit, positive thread count so behavior is
    // reproducible; the CLI defaults this to a detected hardware value.
    int threads = 4;

    float temperature = 0.7f;
    float top_p = 0.9f;
    int top_k = 40;
    int max_tokens = 256;

    // Phase 2: configured memory budget and safety reserve, in megabytes.
    // Defaults follow docs/memory-model.md's illustrative 4GB-device budget
    // (~3000 MB usable, 300 MB held back as reserve) — documented defaults,
    // not invented numbers; override for devices with more or less RAM.
    int64_t memory_budget_mb = 3000;
    int64_t safety_reserve_mb = 300;

    static constexpr int64_t SYJ_EDGEMIND_MIN_MEMORY_BUDGET_MB = 256;
    static constexpr int64_t SYJ_EDGEMIND_MAX_MEMORY_BUDGET_MB = 131072; // 128 GB sanity ceiling

    // Hard ceiling Phase 1 refuses to exceed, independent of any future
    // memory-budget engine. This is a basic "prevent obviously unbounded
    // context allocation" guard required by the Phase 1 spec (§8), not a
    // substitute for Phase 2's real memory accounting.
    static constexpr int SYJ_EDGEMIND_MAX_CONTEXT = 8192;
    static constexpr int SYJ_EDGEMIND_MIN_CONTEXT = 16;

    // v0.3.0: usage/quota limits. 0 means "disabled" for each — see
    // src/usage/usage_types.h's UsagePolicy for the full semantics (this
    // struct's fields map directly onto UsagePolicy; kept separate rather
    // than embedding UsagePolicy here so src/core/config.h doesn't need to
    // depend on src/usage/, mirroring how memory_budget_mb/safety_reserve_mb
    // above don't depend on src/memory/ types either).
    int64_t session_time_limit_seconds = 0;
    int64_t daily_message_limit = 0;
    int64_t daily_token_limit = 0;
    int64_t reset_period_seconds = 86400;

    // Local path for persisted usage state (see UsageStateStore). Defaults
    // to a dotfile in the current working directory — deliberately simple
    // rather than resolving a platform-specific app-data directory, which
    // Android/Termux and desktop platforms handle very differently; a
    // caller that cares can override this explicitly.
    std::string usage_state_path = ".syj_edgemind_usage_state";

    static constexpr int64_t SYJ_EDGEMIND_MAX_SANE_LIMIT = 1'000'000'000;
    static constexpr int64_t SYJ_EDGEMIND_MIN_RESET_PERIOD_SECONDS = 60;

    // Phase 3: model verification. An empty expected_model_checksum_sha256
    // means "no checksum configured" — GGUF structural verification is
    // still mandatory regardless (see ModelVerifier), this only controls
    // whether an additional checksum comparison is performed. Comparison
    // is case-insensitive (see ModelVerifier::verify).
    std::string expected_model_checksum_sha256;

    // Local path for the persisted model registry (see ModelRegistry).
    // Same "simple dotfile in the working directory, override if you care"
    // posture as usage_state_path above, for the same platform-portability
    // reason.
    std::string model_registry_path = ".syj_edgemind_model_registry";
};

// Validates a RuntimeConfig. Returns an empty string if valid, or a
// human-readable error describing the first problem found.
// Does NOT check whether model_path exists on disk — that is a runtime
// (not configuration) error, surfaced separately when the model is loaded.
std::string validate_config(const RuntimeConfig& config);

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_CORE_CONFIG_H
