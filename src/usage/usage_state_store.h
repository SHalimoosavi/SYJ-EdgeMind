#ifndef SYJ_EDGEMIND_USAGE_USAGE_STATE_STORE_H
#define SYJ_EDGEMIND_USAGE_USAGE_STATE_STORE_H

#include <string>

#include "usage/usage_types.h"

namespace syj::edgemind {

// Result of attempting to load persisted usage state. These are
// deliberately distinct outcomes — per the requirement to distinguish
// "state is corrupted" from "no state exists yet" (a fresh install is not
// a corruption event) from "quota is legitimately exhausted" (which
// UsageAccounting::evaluate, not this loader, determines from otherwise-
// valid state).
enum class UsageStateLoadResult {
    Ok,         // file existed and parsed/validated successfully
    NotFound,   // no state file exists yet — caller should start fresh, NOT treat as corrupted
    Corrupted,  // file existed but failed to parse or failed validation — FAIL CLOSED
};

// The only file in src/usage/ that touches the filesystem — mirrors
// MemoryObserver's role as the sole OS-touching bridge for the memory
// subsystem. A small, versioned, line-based local text format is used
// deliberately instead of JSON/a database, to avoid introducing a new
// third-party dependency for what is a handful of integers.
//
// File format (one `key=value` pair per line, after a magic first line):
//   SYJ_EDGEMIND_USAGE_STATE_V1
//   version=1
//   period_start_unix=<int64>
//   messages_used_this_period=<int64>
//   tokens_used_this_period=<int64>
//   session_start_unix=<int64>
//
// Every field is validated on load — see usage_state_store.cpp's
// validate_state(). Never interpret a malformed/partial/unexpected file as
// "unlimited usage": any parse or validation failure returns Corrupted,
// never a permissive default.
class UsageStateStore {
public:
    // Attempts to load state from `path`. On UsageStateLoadResult::Ok,
    // `*out_state` is fully populated and already validated. On NotFound or
    // Corrupted, `*out_state` is left as a fresh, zeroed UsageState — the
    // caller (UsageManager) decides what to do with each case; this
    // function does not itself decide fail-open vs. fail-closed policy.
    static UsageStateLoadResult load(const std::string& path, UsageState* out_state);

    // Atomically persists `state` to `path`: writes to `path + ".tmp"`,
    // flushes, then renames over `path`. On POSIX (Linux/Android/Termux),
    // rename() onto an existing file is atomic, so a process interrupted
    // mid-write leaves the previous valid state file intact rather than a
    // half-written one. Returns false on any I/O failure (temp-file create,
    // write, or rename) — callers must not assume the write succeeded
    // without checking the return value.
    static bool save(const std::string& path, const UsageState& state);

private:
    static bool validate_state(const UsageState& state);
};

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_USAGE_USAGE_STATE_STORE_H
