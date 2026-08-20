#include <cstdio>
#include <fstream>

#include "model/model_registry.h"
#include "../test_temp_dir.h"
#include "test_gguf_fixture.h"

using syj::edgemind::ModelRegistry;
using syj::edgemind::RegistryEntry;
using syj::edgemind::RegistryLoadResult;
using syj::edgemind::VerificationResult;
using syj::edgemind::VerificationStatus;
using syj::edgemind::is_verified;

namespace {
int g_failures = 0;

void check(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++g_failures;
    }
}

std::string registry_path(const char* suffix) {
    return syj::edgemind::test::writable_temp_dir() + "/syj_edgemind_test_registry_" + suffix + ".txt";
}
std::string model_path(const char* suffix) {
    return syj::edgemind::test::writable_temp_dir() + "/syj_edgemind_test_registry_model_" + suffix + ".gguf";
}
} // namespace

int main() {
    // --- Fresh registry: NotFound, empty entries, not an error ---
    {
        const std::string reg = registry_path("fresh");
        std::remove(reg.c_str());
        std::vector<RegistryEntry> entries;
        const RegistryLoadResult r = ModelRegistry::load(reg, &entries);
        check(r == RegistryLoadResult::NotFound, "no registry file yet -> NotFound");
        check(entries.empty(), "NotFound -> entries left empty");
    }

    // --- Save/load round-trip with multiple entries, including fields
    //     that need percent-encoding (path contains '=' and a newline is
    //     NOT included since a real path can't contain one on the
    //     platforms this targets, but '=' is a realistic case: consider a
    //     query-string-like directory name) ---
    {
        const std::string reg = registry_path("roundtrip");
        std::remove(reg.c_str());

        std::vector<RegistryEntry> entries;
        RegistryEntry e1;
        e1.model_id = std::string(64, 'a');
        e1.display_name = "Model One";
        e1.local_path = "/data/data/com.termux/files/home/models/model=v1.gguf"; // exercises '=' escaping
        e1.file_size_bytes = 123456789;
        e1.format = "gguf";
        e1.architecture = "llama";
        e1.quantization = "Q4_K_M";
        e1.verification_status = VerificationStatus::Verified;
        e1.verified_at_unix = 1750000000;
        e1.expected_checksum_sha256 = "";
        entries.push_back(e1);

        RegistryEntry e2;
        e2.model_id = std::string(64, 'b');
        e2.display_name = "";
        e2.local_path = "/tmp/model2.gguf";
        e2.file_size_bytes = 0;
        e2.format = "gguf";
        e2.architecture = "";
        e2.quantization = "";
        e2.verification_status = VerificationStatus::ChecksumMismatch;
        e2.verified_at_unix = 0;
        e2.expected_checksum_sha256 = std::string(64, 'c');
        entries.push_back(e2);

        check(ModelRegistry::save(reg, entries), "save() succeeds for a well-formed entry list");

        std::vector<RegistryEntry> loaded;
        const RegistryLoadResult r = ModelRegistry::load(reg, &loaded);
        check(r == RegistryLoadResult::Ok, "load() after save() -> Ok");
        check(loaded.size() == 2, "load() returns exactly the 2 saved entries");
        if (loaded.size() == 2) {
            check(loaded[0].model_id == e1.model_id, "entry 1 model_id round-trips");
            check(loaded[0].local_path == e1.local_path, "entry 1 local_path (containing '=') round-trips exactly");
            check(loaded[0].file_size_bytes == e1.file_size_bytes, "entry 1 file_size_bytes round-trips");
            check(loaded[0].verification_status == VerificationStatus::Verified, "entry 1 verification_status round-trips");
            check(loaded[1].model_id == e2.model_id, "entry 2 model_id round-trips");
            check(loaded[1].display_name.empty(), "entry 2 empty display_name round-trips as empty");
            check(loaded[1].verification_status == VerificationStatus::ChecksumMismatch, "entry 2 verification_status round-trips");
        }
        std::remove(reg.c_str());
    }

    // --- Corrupted registry: wrong magic line -> Corrupted, not silently
    //     treated as empty ---
    {
        const std::string reg = registry_path("bad_magic");
        {
            std::ofstream f(reg, std::ios::trunc);
            f << "NOT_THE_RIGHT_MAGIC\nmodel_id=abc\n";
        }
        std::vector<RegistryEntry> entries;
        const RegistryLoadResult r = ModelRegistry::load(reg, &entries);
        check(r == RegistryLoadResult::Corrupted, "wrong magic line -> Corrupted");
        check(entries.empty(), "Corrupted -> entries left empty, nothing partially trusted");
        std::remove(reg.c_str());
    }

    // --- Corrupted registry: missing required field in an entry ---
    {
        const std::string reg = registry_path("missing_field");
        {
            std::ofstream f(reg, std::ios::trunc);
            f << "SYJ_EDGEMIND_MODEL_REGISTRY_V1\n";
            f << "model_id=abc\n"; // missing every other required field
            f << "\n";
        }
        std::vector<RegistryEntry> entries;
        const RegistryLoadResult r = ModelRegistry::load(reg, &entries);
        check(r == RegistryLoadResult::Corrupted, "entry missing required fields -> Corrupted");
        std::remove(reg.c_str());
    }

    // --- Corrupted registry: unrecognized field name (fail closed rather
    //     than silently ignoring unknown data) ---
    {
        const std::string reg = registry_path("unknown_field");
        {
            std::ofstream f(reg, std::ios::trunc);
            f << "SYJ_EDGEMIND_MODEL_REGISTRY_V1\n";
            f << "totally_made_up_field=xyz\n";
        }
        std::vector<RegistryEntry> entries;
        const RegistryLoadResult r = ModelRegistry::load(reg, &entries);
        check(r == RegistryLoadResult::Corrupted, "unrecognized field name -> Corrupted");
        std::remove(reg.c_str());
    }

    // --- import_model: valid file -> Verified, new entry added ---
    {
        const std::string reg = registry_path("import_valid");
        const std::string model = model_path("import_valid");
        std::remove(reg.c_str());
        syj::edgemind::test::write_fixture(model, syj::edgemind::test::build_valid_gguf());

        bool was_new = false;
        VerificationResult r = ModelRegistry::import_model(reg, model, "", &was_new);
        check(r.status == VerificationStatus::Verified, "import_model on a valid file -> Verified");
        check(was_new, "first import of a new identity -> was_new_entry == true");

        std::vector<RegistryEntry> entries;
        check(ModelRegistry::load(reg, &entries) == RegistryLoadResult::Ok, "registry loads after import");
        check(entries.size() == 1, "exactly one entry recorded after one import");
        if (!entries.empty()) {
            check(entries[0].model_id == r.identity.sha256_hex, "recorded entry's model_id matches computed identity");
            check(entries[0].architecture == "llama", "recorded entry captured architecture from metadata");
            check(entries[0].local_path == model, "recorded entry's local_path matches the imported path");
        }
        std::remove(reg.c_str());
        std::remove(model.c_str());
    }

    // --- import_model: rejected file (invalid magic) -> registry
    //     untouched, no entry recorded (no identity to key on) ---
    {
        const std::string reg = registry_path("import_rejected");
        const std::string model = model_path("import_rejected");
        std::remove(reg.c_str());
        syj::edgemind::test::write_fixture(model, syj::edgemind::test::build_invalid_magic());

        bool was_new = true; // deliberately pre-set to confirm it gets cleared
        VerificationResult r = ModelRegistry::import_model(reg, model, "", &was_new);
        check(r.status == VerificationStatus::InvalidMagic, "import_model on invalid-magic file -> InvalidMagic");
        check(!is_verified(r.status), "rejected import is not verified");
        check(!was_new, "was_new_entry cleared to false on a rejected import");

        std::vector<RegistryEntry> entries;
        const RegistryLoadResult load_result = ModelRegistry::load(reg, &entries);
        check(load_result == RegistryLoadResult::NotFound,
              "registry file was never created for a rejected import (no identity to record)");
        std::remove(model.c_str());
    }

    // --- Duplicate import: importing the SAME file content twice updates
    //     the existing entry in place rather than creating a second one ---
    {
        const std::string reg = registry_path("dup_import");
        const std::string model_a = model_path("dup_import_a");
        const std::string model_b = model_path("dup_import_b"); // different path, same bytes
        std::remove(reg.c_str());
        const std::string bytes = syj::edgemind::test::build_valid_gguf();
        syj::edgemind::test::write_fixture(model_a, bytes);
        syj::edgemind::test::write_fixture(model_b, bytes);

        bool was_new_1 = false, was_new_2 = false;
        VerificationResult r1 = ModelRegistry::import_model(reg, model_a, "", &was_new_1);
        VerificationResult r2 = ModelRegistry::import_model(reg, model_b, "", &was_new_2);

        check(r1.status == VerificationStatus::Verified && r2.status == VerificationStatus::Verified,
              "both imports of byte-identical content verify successfully");
        check(r1.identity.sha256_hex == r2.identity.sha256_hex, "byte-identical files produce the same identity");
        check(was_new_1, "first import of this identity -> was_new_entry == true");
        check(!was_new_2, "second import of the SAME identity (different path) -> was_new_entry == false");

        std::vector<RegistryEntry> entries;
        check(ModelRegistry::load(reg, &entries) == RegistryLoadResult::Ok, "registry loads after duplicate import");
        check(entries.size() == 1, "duplicate-content import does NOT create a second entry");
        if (!entries.empty()) {
            check(entries[0].local_path == model_b,
                  "duplicate import updates local_path to the most recently imported path");
        }
        std::remove(reg.c_str());
        std::remove(model_a.c_str());
        std::remove(model_b.c_str());
    }

    // --- find_by_id: lookup after import ---
    {
        const std::string reg = registry_path("lookup");
        const std::string model = model_path("lookup");
        std::remove(reg.c_str());
        syj::edgemind::test::write_fixture(model, syj::edgemind::test::build_valid_gguf());

        bool was_new = false;
        VerificationResult r = ModelRegistry::import_model(reg, model, "", &was_new);
        check(r.status == VerificationStatus::Verified, "import for lookup test succeeds");

        RegistryEntry found;
        check(ModelRegistry::find_by_id(reg, r.identity.sha256_hex, &found), "find_by_id locates the imported entry");
        check(found.model_id == r.identity.sha256_hex, "found entry has the expected model_id");

        RegistryEntry not_found;
        check(!ModelRegistry::find_by_id(reg, std::string(64, 'z'), &not_found),
              "find_by_id returns false for an identity that was never imported");

        std::remove(reg.c_str());
        std::remove(model.c_str());
    }

    // --- ChecksumMismatch still gets an identity, and IS recorded (it has
    //     a real identity to key on, unlike structural rejections) ---
    {
        const std::string reg = registry_path("checksum_mismatch_recorded");
        const std::string model = model_path("checksum_mismatch_recorded");
        std::remove(reg.c_str());
        syj::edgemind::test::write_fixture(model, syj::edgemind::test::build_valid_gguf());

        bool was_new = false;
        VerificationResult r = ModelRegistry::import_model(
            reg, model, "0000000000000000000000000000000000000000000000000000000000000000", &was_new);
        check(r.status == VerificationStatus::ChecksumMismatch, "wrong expected checksum -> ChecksumMismatch");
        check(was_new, "checksum-mismatched import still recorded as a new entry (has a real identity)");

        std::vector<RegistryEntry> entries;
        check(ModelRegistry::load(reg, &entries) == RegistryLoadResult::Ok, "registry readable after mismatch import");
        check(entries.size() == 1, "checksum-mismatched entry IS recorded (with that status)");
        if (!entries.empty()) {
            check(entries[0].verification_status == VerificationStatus::ChecksumMismatch,
                  "recorded entry preserves ChecksumMismatch status for later inspection");
        }
        std::remove(reg.c_str());
        std::remove(model.c_str());
    }

    if (g_failures == 0) {
        std::printf("test_model_registry: all checks passed.\n");
        return 0;
    }
    std::fprintf(stderr, "test_model_registry: %d check(s) failed.\n", g_failures);
    return 1;
}
