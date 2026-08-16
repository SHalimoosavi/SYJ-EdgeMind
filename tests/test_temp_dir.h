#ifndef SYJ_EDGEMIND_TESTS_TEST_TEMP_DIR_H
#define SYJ_EDGEMIND_TESTS_TEST_TEMP_DIR_H

// Test-only helper, shared across every tests/*/ subdirectory (originally
// written for tests/usage/, promoted here once tests/model/ needed the same
// writable-directory resolution for registry/fixture tests — see git
// history for the original single-directory version). NOT part of src/ —
// no production code (UsageStateStore, ModelRegistry, etc.) is touched by
// this file or depends on it.
//
// Root cause this exists to work around: on Android/Termux, /tmp exists but
// is owned by shell:shell and is not writable by the Termux app process,
// while $TMPDIR (when set by Termux) and $HOME are both writable and
// support rename(). Hard-coding "/tmp" in a test made filesystem-touching
// code under test fail on that platform — not a bug in the code under
// test, a test-environment assumption bug. This header fixes the test's
// assumption, nothing else.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace syj::edgemind::test {

namespace detail {

// Real, not just is-set: writability is proven with an actual write+remove
// probe, since a directory can exist and be listed in an env var yet still
// reject writes (exactly the Termux /tmp case this header exists for).
inline bool directory_is_writable(const std::string& dir) {
    if (dir.empty()) {
        return false;
    }
    const std::string probe_path = dir + "/.syj_edgemind_writetest";
    {
        std::ofstream probe(probe_path, std::ios::trunc);
        if (!probe.is_open()) {
            return false;
        }
        probe << "x";
        probe.flush();
        if (!probe.good()) {
            return false;
        }
    }
    const bool removed = (std::remove(probe_path.c_str()) == 0);
    return removed;
}

// Strips a single trailing slash, if present, so callers can join with "/"
// uniformly without producing "//" in the result.
inline std::string strip_trailing_slash(std::string dir) {
    if (!dir.empty() && dir.back() == '/') {
        dir.pop_back();
    }
    return dir;
}

} // namespace detail

// Resolves a writable temp directory for tests, in this order:
//   1. $TMPDIR, if set and actually writable (preferred — this is what
//      Termux sets to a writable location, unlike its /tmp).
//   2. $HOME, if set and actually writable (Termux's fallback — confirmed
//      writable per the root-cause report).
//   3. "/tmp", if set... i.e. if actually writable (the common case on
//      desktop Linux/macOS where TMPDIR/HOME checks above didn't apply or
//      weren't set).
//   4. "." (current working directory) as a last resort, so tests degrade
//      to "wherever ctest runs from" rather than hard-failing outright if
//      none of the above are usable.
//
// Resolved once and cached for the process lifetime — the environment
// doesn't change mid-test-run, and repeated probe writes would just be
// wasted I/O.
inline const std::string& writable_temp_dir() {
    static const std::string resolved = [] {
        if (const char* tmpdir = std::getenv("TMPDIR")) {
            const std::string candidate = detail::strip_trailing_slash(tmpdir);
            if (detail::directory_is_writable(candidate)) {
                return candidate;
            }
        }
        if (const char* home = std::getenv("HOME")) {
            const std::string candidate = detail::strip_trailing_slash(home);
            if (detail::directory_is_writable(candidate)) {
                return candidate;
            }
        }
        if (detail::directory_is_writable("/tmp")) {
            return std::string("/tmp");
        }
        return std::string(".");
    }();
    return resolved;
}

} // namespace syj::edgemind::test

#endif // SYJ_EDGEMIND_TESTS_TEST_TEMP_DIR_H
