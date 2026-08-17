#include <cstdio>
#include <fstream>

#include "model/model_resolver.h"
#include "model/model_registry.h"
#include "../test_temp_dir.h"
#include "test_gguf_fixture.h"

using syj::edgemind::ModelResolutionResult;
using syj::edgemind::ModelResolutionStatus;
using syj::edgemind::ModelRegistry;
using syj::edgemind::resolve_model_path;

namespace {
int g_failures = 0;

void check(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++g_failures;
    }
}

std::string registry_path(const char* suffix) {
    return syj::edgemind::test::writable_temp_dir() + "/syj_edgemind_test_resolver_registry_" + suffix + ".txt";
}
std::string model_path(const char* suffix) {
    return syj::edgemind::test::writable_temp_dir() + "/syj_edgemind_test_resolver_model_" + suffix + ".gguf";
}
} // namespace

int main() {
    // --- Direct path flow: returned as-is, no filesystem/registry touch at all ---
    {
        ModelResolutionResult r = resolve_model_path("/some/direct/path.gguf", "", "irrelevant_registry.txt");
        check(r.status == ModelResolutionStatus::Resolved, "direct model_path -> Resolved");
        check(r.resolved_path == "/some/direct/path.gguf", "direct model_path is returned verbatim, unmodified");
    }

    // --- Neither provided ---
    {
        ModelResolutionResult r = resolve_model_path("", "", "irrelevant_registry.txt");
        check(r.status == ModelResolutionStatus::NeitherProvided, "neither model_path nor model_id -> NeitherProvided");
        check(r.resolved_path.empty(), "resolved_path stays empty on NeitherProvided");
    }

    // --- Both provided: rejected, no precedence guess ---
    {
        ModelResolutionResult r = resolve_model_path("/some/path.gguf", std::string(64, 'a'), "irrelevant_registry.txt");
        check(r.status == ModelResolutionStatus::BothProvided, "both model_path and model_id -> BothProvided (rejected)");
        check(r.resolved_path.empty(), "resolved_path stays empty on BothProvided");
    }

    // --- model_id flow: registry doesn't exist yet -> ModelIdNotFound (not RegistryUnreadable) ---
    {
        const std::string reg = registry_path("fresh");
        std::remove(reg.c_str());
        ModelResolutionResult r = resolve_model_path("", std::string(64, 'b'), reg);
        check(r.status == ModelResolutionStatus::ModelIdNotFound,
              "model_id given, registry file doesn't exist yet -> ModelIdNotFound, not RegistryUnreadable");
    }

    // --- model_id flow: registry exists, but no matching entry ---
    {
        const std::string reg = registry_path("no_match");
        const std::string model = model_path("no_match");
        std::remove(reg.c_str());
        syj::edgemind::test::write_fixture(model, syj::edgemind::test::build_valid_gguf());
        bool was_new = false;
        ModelRegistry::import_model(reg, model, "", &was_new); // populates the registry with SOME entry

        ModelResolutionResult r = resolve_model_path("", std::string(64, 'z'), reg); // different id
        check(r.status == ModelResolutionStatus::ModelIdNotFound,
              "model_id given, registry has entries but none match -> ModelIdNotFound");
        std::remove(reg.c_str());
        std::remove(model.c_str());
    }

    // --- model_id flow: real match -> resolves to the registry's local_path ---
    {
        const std::string reg = registry_path("match");
        const std::string model = model_path("match");
        std::remove(reg.c_str());
        syj::edgemind::test::write_fixture(model, syj::edgemind::test::build_valid_gguf());
        bool was_new = false;
        auto verify_result = ModelRegistry::import_model(reg, model, "", &was_new);
        check(was_new, "test setup: import created a new entry");

        ModelResolutionResult r = resolve_model_path("", verify_result.identity.sha256_hex, reg);
        check(r.status == ModelResolutionStatus::Resolved, "matching model_id -> Resolved");
        check(r.resolved_path == model, "resolved_path equals the registry entry's local_path");
        std::remove(reg.c_str());
        std::remove(model.c_str());
    }

    // --- model_id flow: corrupted registry -> RegistryUnreadable, distinct from ModelIdNotFound ---
    {
        const std::string reg = registry_path("corrupted");
        {
            std::ofstream f(reg, std::ios::trunc);
            f << "NOT_THE_RIGHT_MAGIC_LINE\n";
        }
        ModelResolutionResult r = resolve_model_path("", std::string(64, 'c'), reg);
        check(r.status == ModelResolutionStatus::RegistryUnreadable,
              "corrupted registry file -> RegistryUnreadable, distinct from ModelIdNotFound");
        std::remove(reg.c_str());
    }

    // --- Resolution does NOT mutate the registry: importing then resolving
    //     twice must leave the registry file byte-identical (resolver is
    //     read-only, per its documented contract) ---
    {
        const std::string reg = registry_path("no_mutation");
        const std::string model = model_path("no_mutation");
        std::remove(reg.c_str());
        syj::edgemind::test::write_fixture(model, syj::edgemind::test::build_valid_gguf());
        bool was_new = false;
        auto verify_result = ModelRegistry::import_model(reg, model, "", &was_new);

        std::ifstream before(reg, std::ios::binary);
        std::string before_contents((std::istreambuf_iterator<char>(before)), std::istreambuf_iterator<char>());

        resolve_model_path("", verify_result.identity.sha256_hex, reg);
        resolve_model_path("", verify_result.identity.sha256_hex, reg);

        std::ifstream after(reg, std::ios::binary);
        std::string after_contents((std::istreambuf_iterator<char>(after)), std::istreambuf_iterator<char>());

        check(before_contents == after_contents, "resolve_model_path never mutates the registry file");
        std::remove(reg.c_str());
        std::remove(model.c_str());
    }

    if (g_failures == 0) {
        std::printf("test_model_resolver: all checks passed.\n");
        return 0;
    }
    std::fprintf(stderr, "test_model_resolver: %d check(s) failed.\n", g_failures);
    return 1;
}
