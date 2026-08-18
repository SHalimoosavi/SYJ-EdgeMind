#include <cstdio>
#include <fstream>

#include "core/model_selection.h"
#include "model/model_registry.h"
#include "../test_temp_dir.h"
#include "../model/test_gguf_fixture.h"

using syj::edgemind::CliModelSelectionOutcome;
using syj::edgemind::CliModelSelectionResult;
using syj::edgemind::ModelRegistry;
using syj::edgemind::select_model_for_cli;

namespace {
int g_failures = 0;

void check(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++g_failures;
    }
}

std::string registry_path(const char* suffix) {
    return syj::edgemind::test::writable_temp_dir() + "/syj_edgemind_test_selection_registry_" + suffix + ".txt";
}
std::string model_path(const char* suffix) {
    return syj::edgemind::test::writable_temp_dir() + "/syj_edgemind_test_selection_model_" + suffix + ".gguf";
}
} // namespace

int main() {
    // --- 0 registered entries: registry file doesn't exist yet ---
    {
        const std::string reg = registry_path("zero_fresh");
        std::remove(reg.c_str());
        CliModelSelectionResult r = select_model_for_cli(reg);
        check(r.outcome == CliModelSelectionOutcome::NoRegisteredModels,
              "no registry file yet -> NoRegisteredModels");
        check(r.all_entries.empty(), "NoRegisteredModels -> all_entries stays empty");
        check(r.selected_entry.model_id.empty(), "NoRegisteredModels -> selected_entry stays default/empty");
    }

    // --- 0 registered entries: corrupted registry also collapses to NoRegisteredModels ---
    {
        const std::string reg = registry_path("zero_corrupted");
        {
            std::ofstream f(reg, std::ios::trunc);
            f << "NOT_THE_RIGHT_MAGIC_LINE\n";
        }
        CliModelSelectionResult r = select_model_for_cli(reg);
        check(r.outcome == CliModelSelectionOutcome::NoRegisteredModels,
              "corrupted registry -> NoRegisteredModels (same actionable guidance as genuinely empty)");
        std::remove(reg.c_str());
    }

    // --- exactly 1 registered entry: auto-selected, with real identity ---
    {
        const std::string reg = registry_path("one");
        const std::string model = model_path("one");
        std::remove(reg.c_str());
        syj::edgemind::test::write_fixture(model, syj::edgemind::test::build_valid_gguf());
        bool was_new = false;
        auto verify_result = ModelRegistry::import_model(reg, model, "", &was_new);
        check(was_new, "test setup: import created a new entry");

        CliModelSelectionResult r = select_model_for_cli(reg);
        check(r.outcome == CliModelSelectionOutcome::SingleModelSelected,
              "exactly one registered entry -> SingleModelSelected");
        check(r.selected_entry.model_id == verify_result.identity.sha256_hex,
              "selected_entry.model_id matches the one real registered identity");
        check(r.selected_entry.local_path == model, "selected_entry.local_path matches the imported path");
        check(r.all_entries.size() == 1, "all_entries also reflects the single entry");
        std::remove(reg.c_str());
        std::remove(model.c_str());
    }

    // --- 2+ registered entries: no guess, explicit choice required ---
    {
        const std::string reg = registry_path("two");
        const std::string model_a = model_path("two_a");
        const std::string model_b = model_path("two_b");
        std::remove(reg.c_str());
        // Two DIFFERENT byte-contents so they get two distinct identities
        // (a duplicate-content import would collapse to one entry, which
        // is exactly v0.5.0's existing dedup behavior — not what this test
        // is checking).
        std::string bytes_a = syj::edgemind::test::build_valid_gguf(3);
        std::string bytes_b = syj::edgemind::test::build_valid_gguf(2); // different gguf_version -> different bytes -> different hash
        syj::edgemind::test::write_fixture(model_a, bytes_a);
        syj::edgemind::test::write_fixture(model_b, bytes_b);
        bool was_new_a = false, was_new_b = false;
        auto r_a = ModelRegistry::import_model(reg, model_a, "", &was_new_a);
        auto r_b = ModelRegistry::import_model(reg, model_b, "", &was_new_b);
        check(was_new_a && was_new_b && r_a.identity.sha256_hex != r_b.identity.sha256_hex,
              "test setup: two distinct real identities registered");

        CliModelSelectionResult r = select_model_for_cli(reg);
        check(r.outcome == CliModelSelectionOutcome::MultipleModelsRequireChoice,
              "two registered entries -> MultipleModelsRequireChoice, never a guess");
        check(r.selected_entry.model_id.empty(),
              "MultipleModelsRequireChoice -> selected_entry stays empty (no silent pick)");
        check(r.all_entries.size() == 2, "all_entries reflects both registered entries for display");
        std::remove(reg.c_str());
        std::remove(model_a.c_str());
        std::remove(model_b.c_str());
    }

    // --- Determinism: calling twice against the same registry produces
    //     the same outcome and, for SingleModelSelected, the same
    //     selected_entry.model_id ---
    {
        const std::string reg = registry_path("deterministic");
        const std::string model = model_path("deterministic");
        std::remove(reg.c_str());
        syj::edgemind::test::write_fixture(model, syj::edgemind::test::build_valid_gguf());
        bool was_new = false;
        ModelRegistry::import_model(reg, model, "", &was_new);

        CliModelSelectionResult r1 = select_model_for_cli(reg);
        CliModelSelectionResult r2 = select_model_for_cli(reg);
        check(r1.outcome == r2.outcome, "repeated calls against an unchanged registry produce the same outcome");
        check(r1.selected_entry.model_id == r2.selected_entry.model_id,
              "repeated calls select the same model_id — deterministic, not order-dependent");
        std::remove(reg.c_str());
        std::remove(model.c_str());
    }

    // --- select_model_for_cli never mutates the registry (read-only, per
    //     its documented contract) ---
    {
        const std::string reg = registry_path("no_mutation");
        const std::string model = model_path("no_mutation");
        std::remove(reg.c_str());
        syj::edgemind::test::write_fixture(model, syj::edgemind::test::build_valid_gguf());
        bool was_new = false;
        ModelRegistry::import_model(reg, model, "", &was_new);

        std::ifstream before(reg, std::ios::binary);
        std::string before_contents((std::istreambuf_iterator<char>(before)), std::istreambuf_iterator<char>());

        select_model_for_cli(reg);
        select_model_for_cli(reg);

        std::ifstream after(reg, std::ios::binary);
        std::string after_contents((std::istreambuf_iterator<char>(after)), std::istreambuf_iterator<char>());

        check(before_contents == after_contents, "select_model_for_cli never mutates the registry file");
        std::remove(reg.c_str());
        std::remove(model.c_str());
    }

    if (g_failures == 0) {
        std::printf("test_model_selection: all checks passed.\n");
        return 0;
    }
    std::fprintf(stderr, "test_model_selection: %d check(s) failed.\n", g_failures);
    return 1;
}
