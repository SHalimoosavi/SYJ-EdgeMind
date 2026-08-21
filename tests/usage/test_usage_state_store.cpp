// Unlike the memory subsystem's pure components, UsageStateStore genuinely
// does file I/O — so this test genuinely exercises the real filesystem
// (a temp file under the OS temp directory), not a mock. It cleans up
// after itself.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "usage/usage_state_store.h"
#include "../test_temp_dir.h"

using syj::edgemind::UsageState;
using syj::edgemind::UsageStateLoadResult;
using syj::edgemind::UsageStateStore;

namespace {
int g_failures = 0;

void check(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++g_failures;
    }
}

std::string temp_path(const char* suffix) {
    return syj::edgemind::test::writable_temp_dir() + "/syj_edgemind_test_usage_state_" + suffix + ".txt";
}

void remove_if_exists(const std::string& path) {
    std::remove(path.c_str());
    std::remove((path + ".tmp").c_str());
}
} // namespace

int main() {
    // Loading a path that doesn't exist yields NotFound, not Corrupted —
    // a fresh install must not be treated as a corruption event.
    {
        const std::string path = temp_path("nonexistent");
        remove_if_exists(path);

        UsageState state;
        const UsageStateLoadResult result = UsageStateStore::load(path, &state);
        check(result == UsageStateLoadResult::NotFound, "loading a nonexistent path returns NotFound");
    }

    // Save then load round-trips exactly.
    {
        const std::string path = temp_path("roundtrip");
        remove_if_exists(path);

        UsageState original;
        original.period_start_unix = 1700000000;
        original.messages_used_this_period = 42;
        original.tokens_used_this_period = 12345;
        original.session_start_unix = 1700000500;

        check(UsageStateStore::save(path, original), "save() succeeds for a valid state");

        UsageState loaded;
        const UsageStateLoadResult result = UsageStateStore::load(path, &loaded);
        check(result == UsageStateLoadResult::Ok, "load() after a successful save returns Ok");
        check(loaded.version == original.version, "round-tripped version matches");
        check(loaded.period_start_unix == original.period_start_unix, "round-tripped period_start_unix matches");
        check(loaded.messages_used_this_period == original.messages_used_this_period,
              "round-tripped messages_used_this_period matches");
        check(loaded.tokens_used_this_period == original.tokens_used_this_period,
              "round-tripped tokens_used_this_period matches");
        check(loaded.session_start_unix == original.session_start_unix, "round-tripped session_start_unix matches");

        remove_if_exists(path);
    }

    // A completely garbage file is Corrupted, not silently defaulted.
    {
        const std::string path = temp_path("garbage");
        remove_if_exists(path);
        {
            std::ofstream out(path);
            out << "this is not a usage state file at all\n";
        }

        UsageState state;
        const UsageStateLoadResult result = UsageStateStore::load(path, &state);
        check(result == UsageStateLoadResult::Corrupted, "a garbage file is reported as Corrupted");

        remove_if_exists(path);
    }

    // A file with the right magic line but a missing required field is
    // Corrupted — fail closed on partial data, don't fill in a default.
    {
        const std::string path = temp_path("missing_field");
        remove_if_exists(path);
        {
            std::ofstream out(path);
            out << "SYJ_EDGEMIND_USAGE_STATE_V1\n";
            out << "version=1\n";
            out << "period_start_unix=1000\n";
            // messages_used_this_period, tokens_used_this_period,
            // session_start_unix deliberately omitted.
        }

        UsageState state;
        const UsageStateLoadResult result = UsageStateStore::load(path, &state);
        check(result == UsageStateLoadResult::Corrupted, "a file missing required fields is Corrupted");

        remove_if_exists(path);
    }

    // An unrecognized version number is Corrupted — never guess a migration.
    {
        const std::string path = temp_path("bad_version");
        remove_if_exists(path);
        {
            std::ofstream out(path);
            out << "SYJ_EDGEMIND_USAGE_STATE_V1\n";
            out << "version=999\n";
            out << "period_start_unix=1000\n";
            out << "messages_used_this_period=0\n";
            out << "tokens_used_this_period=0\n";
            out << "session_start_unix=0\n";
        }

        UsageState state;
        const UsageStateLoadResult result = UsageStateStore::load(path, &state);
        check(result == UsageStateLoadResult::Corrupted, "an unrecognized version number is Corrupted");

        remove_if_exists(path);
    }

    // Negative counters are Corrupted — never trust a negative usage count.
    {
        const std::string path = temp_path("negative_counter");
        remove_if_exists(path);
        {
            std::ofstream out(path);
            out << "SYJ_EDGEMIND_USAGE_STATE_V1\n";
            out << "version=1\n";
            out << "period_start_unix=1000\n";
            out << "messages_used_this_period=-5\n";
            out << "tokens_used_this_period=0\n";
            out << "session_start_unix=0\n";
        }

        UsageState state;
        const UsageStateLoadResult result = UsageStateStore::load(path, &state);
        check(result == UsageStateLoadResult::Corrupted, "a negative counter is Corrupted");

        remove_if_exists(path);
    }

    // An absurd timestamp (far future) is Corrupted.
    {
        const std::string path = temp_path("absurd_timestamp");
        remove_if_exists(path);
        {
            std::ofstream out(path);
            out << "SYJ_EDGEMIND_USAGE_STATE_V1\n";
            out << "version=1\n";
            out << "period_start_unix=99999999999999\n"; // way past year 2100
            out << "messages_used_this_period=0\n";
            out << "tokens_used_this_period=0\n";
            out << "session_start_unix=0\n";
        }

        UsageState state;
        const UsageStateLoadResult result = UsageStateStore::load(path, &state);
        check(result == UsageStateLoadResult::Corrupted, "an absurd far-future timestamp is Corrupted");

        remove_if_exists(path);
    }

    // Non-numeric field value is Corrupted, not silently parsed as 0.
    {
        const std::string path = temp_path("non_numeric");
        remove_if_exists(path);
        {
            std::ofstream out(path);
            out << "SYJ_EDGEMIND_USAGE_STATE_V1\n";
            out << "version=1\n";
            out << "period_start_unix=not_a_number\n";
            out << "messages_used_this_period=0\n";
            out << "tokens_used_this_period=0\n";
            out << "session_start_unix=0\n";
        }

        UsageState state;
        const UsageStateLoadResult result = UsageStateStore::load(path, &state);
        check(result == UsageStateLoadResult::Corrupted, "a non-numeric field value is Corrupted, not parsed as 0");

        remove_if_exists(path);
    }

    // save() leaves no stray .tmp file behind on success (atomic
    // write-then-rename cleaned up correctly).
    {
        const std::string path = temp_path("no_stray_tmp");
        remove_if_exists(path);

        UsageState state;
        state.period_start_unix = 1000;
        check(UsageStateStore::save(path, state), "save() succeeds");

        std::ifstream tmp_check(path + ".tmp");
        check(!tmp_check.is_open(), "no stray .tmp file remains after a successful save (rename succeeded)");

        remove_if_exists(path);
    }

    // Phase 6: save() a second time to a path that already holds a
    // PREVIOUS successful save (not a fresh path) — the exact scenario
    // Phase 5's Windows persistence fix exists for (POSIX rename()
    // atomically replaces an existing destination; Windows'
    // std::rename/_wrename does not, hence the MoveFileExA branch — see
    // src/usage/usage_state_store.cpp). This test only ever compiles and
    // runs the POSIX branch in this sandbox, so it proves the POSIX path
    // is unaffected by that change, not that the Windows branch works —
    // that remains real-Windows-hardware-only, as always. Confirms the
    // LATEST save wins on reload, not a stale or corrupted intermediate
    // state.
    {
        const std::string path = temp_path("double_save_existing_destination");
        remove_if_exists(path);

        UsageState first;
        first.period_start_unix = 1111;
        first.messages_used_this_period = 1;
        check(UsageStateStore::save(path, first), "first save() to a fresh path succeeds");

        UsageState second;
        second.period_start_unix = 2222;
        second.messages_used_this_period = 2;
        check(UsageStateStore::save(path, second),
              "second save() to the SAME, already-existing destination succeeds "
              "(exercises replacing an existing file, not just creating a new one)");

        UsageState loaded;
        const UsageStateLoadResult load_result = UsageStateStore::load(path, &loaded);
        check(load_result == UsageStateLoadResult::Ok, "load() after the second save succeeds");
        check(loaded.period_start_unix == 2222 && loaded.messages_used_this_period == 2,
              "reloaded state reflects the SECOND save, not the first — the replace actually "
              "replaced, it didn't merge, corrupt, or silently keep the old content");

        std::ifstream tmp_check2(path + ".tmp");
        check(!tmp_check2.is_open(), "no stray .tmp file remains after the second save either");

        remove_if_exists(path);
    }

    if (g_failures > 0) {
        std::fprintf(stderr, "%d check(s) failed.\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("test_usage_state_store: all checks passed.\n");
    return EXIT_SUCCESS;
}
