#include "usage/usage_state_store.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace syj::edgemind {

namespace {
constexpr const char* kMagicLine = "SYJ_EDGEMIND_USAGE_STATE_V1";

// Strict int64 parse: the whole string must be consumed, no trailing junk.
// Returns false (leaving *out unspecified) on any malformed input —
// consistent with this project's "fail closed, never invent a number"
// convention (see MemoryEstimator's checked_mul).
bool strict_parse_i64(const std::string& s, int64_t* out) {
    if (s.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    if (end != s.c_str() + s.size() || errno == ERANGE) {
        return false;
    }
    *out = static_cast<int64_t>(v);
    return true;
}
} // namespace

UsageStateLoadResult UsageStateStore::load(const std::string& path, UsageState* out_state) {
    // A fresh UsageState{} already carries the correct current
    // SYJ_EDGEMIND_USAGE_STATE_VERSION via its default member initializer
    // (see usage_types.h) — deliberately NOT overwritten here. The
    // NotFound/Corrupted/Ok return code is what tells the caller whether
    // this is genuinely fresh state; *out_state itself must always be
    // immediately usable (including save()-able) on NotFound, since
    // UsageManager does exactly that for a first-ever run.
    *out_state = UsageState{};

    std::ifstream in(path);
    if (!in.is_open()) {
        return UsageStateLoadResult::NotFound;
    }

    std::string magic_line;
    if (!std::getline(in, magic_line) || magic_line != kMagicLine) {
        return UsageStateLoadResult::Corrupted;
    }

    std::map<std::string, std::string> fields;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0) {
            return UsageStateLoadResult::Corrupted; // malformed line — fail closed, don't skip-and-hope
        }
        fields[line.substr(0, eq)] = line.substr(eq + 1);
    }

    const char* required_keys[] = {
        "version", "period_start_unix", "messages_used_this_period", "tokens_used_this_period", "session_start_unix",
    };
    for (const char* key : required_keys) {
        if (fields.find(key) == fields.end()) {
            return UsageStateLoadResult::Corrupted; // missing field — fail closed
        }
    }

    UsageState parsed;
    {
        int64_t version64 = 0;
        if (!strict_parse_i64(fields["version"], &version64)) {
            return UsageStateLoadResult::Corrupted;
        }
        if (version64 < 0 || version64 > 0x7fffffffLL) {
            return UsageStateLoadResult::Corrupted;
        }
        parsed.version = static_cast<int32_t>(version64);
    }

    if (!strict_parse_i64(fields["period_start_unix"], &parsed.period_start_unix)) return UsageStateLoadResult::Corrupted;
    if (!strict_parse_i64(fields["messages_used_this_period"], &parsed.messages_used_this_period)) return UsageStateLoadResult::Corrupted;
    if (!strict_parse_i64(fields["tokens_used_this_period"], &parsed.tokens_used_this_period)) return UsageStateLoadResult::Corrupted;
    if (!strict_parse_i64(fields["session_start_unix"], &parsed.session_start_unix)) return UsageStateLoadResult::Corrupted;

    if (!validate_state(parsed)) {
        return UsageStateLoadResult::Corrupted;
    }

    *out_state = parsed;
    return UsageStateLoadResult::Ok;
}

bool UsageStateStore::validate_state(const UsageState& state) {
    if (state.version != UsageState::SYJ_EDGEMIND_USAGE_STATE_VERSION) {
        return false; // unrecognized version — fail closed rather than guess at a migration
    }
    if (state.period_start_unix < 0 || state.period_start_unix > UsageState::SYJ_EDGEMIND_MAX_SANE_TIMESTAMP) {
        return false;
    }
    if (state.session_start_unix < 0 || state.session_start_unix > UsageState::SYJ_EDGEMIND_MAX_SANE_TIMESTAMP) {
        return false;
    }
    if (state.messages_used_this_period < 0 || state.messages_used_this_period > UsageState::SYJ_EDGEMIND_MAX_SANE_COUNTER) {
        return false;
    }
    if (state.tokens_used_this_period < 0 || state.tokens_used_this_period > UsageState::SYJ_EDGEMIND_MAX_SANE_COUNTER) {
        return false;
    }
    return true;
}

bool UsageStateStore::save(const std::string& path, const UsageState& state) {
    if (!validate_state(state)) {
        return false; // never persist a state we wouldn't accept back on load
    }

    const std::string tmp_path = path + ".tmp";

    {
        std::ofstream out(tmp_path, std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out << kMagicLine << "\n";
        out << "version=" << state.version << "\n";
        out << "period_start_unix=" << state.period_start_unix << "\n";
        out << "messages_used_this_period=" << state.messages_used_this_period << "\n";
        out << "tokens_used_this_period=" << state.tokens_used_this_period << "\n";
        out << "session_start_unix=" << state.session_start_unix << "\n";
        out.flush();
        if (!out.good()) {
            return false;
        }
    } // out closed here, before rename — required on some platforms

    // Atomic on POSIX (Linux/Android/Termux): rename() onto an existing
    // path replaces it atomically, so a crash between the write above and
    // this rename leaves the OLD state file intact, never a half-written
    // new one.
    if (std::rename(tmp_path.c_str(), path.c_str()) != 0) {
        std::remove(tmp_path.c_str()); // best-effort cleanup; failure here doesn't change the overall result
        return false;
    }

    return true;
}

} // namespace syj::edgemind
