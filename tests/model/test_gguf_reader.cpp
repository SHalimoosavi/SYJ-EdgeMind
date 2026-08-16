#include <cstdio>

#include "model/gguf_reader.h"
#include "../test_temp_dir.h"
#include "test_gguf_fixture.h"

using syj::edgemind::GgufReader;
using syj::edgemind::GgufValidationStatus;
using syj::edgemind::ModelMetadata;
using syj::edgemind::gguf_validation_status_message;

namespace {
int g_failures = 0;

void check(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++g_failures;
    }
}

std::string fixture_path(const char* suffix) {
    return syj::edgemind::test::writable_temp_dir() + "/syj_edgemind_test_gguf_" + suffix + ".gguf";
}
} // namespace

int main() {
    // --- Valid file (v3): full metadata extraction ---
    {
        const std::string path = fixture_path("valid_v3");
        check(syj::edgemind::test::write_fixture(path, syj::edgemind::test::build_valid_gguf(3)),
              "valid v3 fixture written");
        ModelMetadata md;
        const GgufValidationStatus st = GgufReader::validate(path, md);
        check(st == GgufValidationStatus::Valid, "valid v3 fixture -> Valid");
        check(md.gguf_version == 3, "valid v3 fixture -> gguf_version == 3");
        check(md.tensor_count == 0, "valid v3 fixture -> tensor_count == 0");
        check(md.metadata_kv_count == 6, "valid v3 fixture -> metadata_kv_count == 6");
        check(md.architecture_present && md.architecture == "llama", "valid v3 fixture -> architecture == llama");
        check(md.name_present && md.name == "test-model", "valid v3 fixture -> name == test-model");
        check(md.quantization_version_present && md.quantization_version == 2,
              "valid v3 fixture -> quantization_version == 2");
        check(md.file_type_present && md.file_type == 15, "valid v3 fixture -> file_type == 15");
        check(md.context_length_present && md.context_length == 2048,
              "valid v3 fixture -> context_length == 2048 (resolved via architecture-prefixed key)");
        std::remove(path.c_str());
    }

    // --- Valid file (v2): same shape, older header version ---
    {
        const std::string path = fixture_path("valid_v2");
        check(syj::edgemind::test::write_fixture(path, syj::edgemind::test::build_valid_gguf(2)),
              "valid v2 fixture written");
        ModelMetadata md;
        const GgufValidationStatus st = GgufReader::validate(path, md);
        check(st == GgufValidationStatus::Valid, "valid v2 fixture -> Valid");
        check(md.gguf_version == 2, "valid v2 fixture -> gguf_version == 2");
        std::remove(path.c_str());
    }

    // --- Missing architecture key: partial metadata, no crash ---
    {
        const std::string path = fixture_path("no_arch");
        check(syj::edgemind::test::write_fixture(path, syj::edgemind::test::build_no_architecture_gguf()),
              "no-architecture fixture written");
        ModelMetadata md;
        const GgufValidationStatus st = GgufReader::validate(path, md);
        check(st == GgufValidationStatus::Valid, "no-architecture fixture -> still structurally Valid");
        check(!md.architecture_present, "no-architecture fixture -> architecture_present == false");
        check(!md.context_length_present, "no-architecture fixture -> context_length_present == false (no architecture to key on)");
        check(md.name_present && md.name == "no-arch-model", "no-architecture fixture -> name still captured");
        check(md.file_type_present && md.file_type == 0, "no-architecture fixture -> file_type == 0 (F32)");
        std::remove(path.c_str());
    }

    // --- Nonexistent path ---
    {
        ModelMetadata md;
        const GgufValidationStatus st = GgufReader::validate(fixture_path("does_not_exist_xyz"), md);
        check(st == GgufValidationStatus::PathNotFound, "nonexistent path -> PathNotFound");
    }

    // --- Empty file ---
    {
        const std::string path = fixture_path("empty");
        check(syj::edgemind::test::write_fixture(path, ""), "empty fixture written");
        ModelMetadata md;
        const GgufValidationStatus st = GgufReader::validate(path, md);
        check(st == GgufValidationStatus::FileEmpty, "empty fixture -> FileEmpty");
        std::remove(path.c_str());
    }

    // --- Invalid magic ---
    {
        const std::string path = fixture_path("bad_magic");
        check(syj::edgemind::test::write_fixture(path, syj::edgemind::test::build_invalid_magic()),
              "invalid-magic fixture written");
        ModelMetadata md;
        const GgufValidationStatus st = GgufReader::validate(path, md);
        check(st == GgufValidationStatus::InvalidMagic, "invalid-magic fixture -> InvalidMagic");
        std::remove(path.c_str());
    }

    // --- Unsupported version ---
    {
        const std::string path = fixture_path("bad_version");
        check(syj::edgemind::test::write_fixture(path, syj::edgemind::test::build_unsupported_version()),
              "unsupported-version fixture written");
        ModelMetadata md;
        const GgufValidationStatus st = GgufReader::validate(path, md);
        check(st == GgufValidationStatus::UnsupportedVersion, "unsupported-version fixture -> UnsupportedVersion");
        std::remove(path.c_str());
    }

    // --- Truncated header ---
    {
        const std::string path = fixture_path("trunc_header");
        check(syj::edgemind::test::write_fixture(path, syj::edgemind::test::build_truncated_header()),
              "truncated-header fixture written");
        ModelMetadata md;
        const GgufValidationStatus st = GgufReader::validate(path, md);
        check(st == GgufValidationStatus::TruncatedHeader, "truncated-header fixture -> TruncatedHeader");
        std::remove(path.c_str());
    }

    // --- Truncated metadata (valid header, KV section cut short) ---
    {
        const std::string path = fixture_path("trunc_meta");
        check(syj::edgemind::test::write_fixture(path, syj::edgemind::test::build_truncated_metadata()),
              "truncated-metadata fixture written");
        ModelMetadata md;
        const GgufValidationStatus st = GgufReader::validate(path, md);
        check(st == GgufValidationStatus::MalformedMetadata, "truncated-metadata fixture -> MalformedMetadata");
        std::remove(path.c_str());
    }

    // --- Malformed: absurd string length claimed ---
    {
        const std::string path = fixture_path("huge_str");
        check(syj::edgemind::test::write_fixture(path, syj::edgemind::test::build_malformed_huge_string_len()),
              "huge-string-length fixture written");
        ModelMetadata md;
        const GgufValidationStatus st = GgufReader::validate(path, md);
        check(st == GgufValidationStatus::MalformedMetadata,
              "huge-string-length fixture -> MalformedMetadata (rejected before any allocation)");
        std::remove(path.c_str());
    }

    // --- Malformed: absurd kv_count claimed, no data ---
    {
        const std::string path = fixture_path("huge_kv");
        check(syj::edgemind::test::write_fixture(path, syj::edgemind::test::build_malformed_huge_kv_count()),
              "huge-kv-count fixture written");
        ModelMetadata md;
        const GgufValidationStatus st = GgufReader::validate(path, md);
        check(st == GgufValidationStatus::MalformedMetadata,
              "huge-kv-count fixture -> MalformedMetadata (rejected by sane-ceiling check)");
        std::remove(path.c_str());
    }

    // --- Malformed: unknown value type code ---
    {
        const std::string path = fixture_path("bad_type");
        check(syj::edgemind::test::write_fixture(path, syj::edgemind::test::build_malformed_unknown_type()),
              "unknown-type fixture written");
        ModelMetadata md;
        const GgufValidationStatus st = GgufReader::validate(path, md);
        check(st == GgufValidationStatus::MalformedMetadata, "unknown-type fixture -> MalformedMetadata");
        std::remove(path.c_str());
    }

    // --- Status messages are non-null/non-empty for every enumerator (a
    // programmatic classification check, not string-matching for logic —
    // this just confirms the switch in model_types.cpp stayed exhaustive) ---
    {
        const GgufValidationStatus all[] = {
            GgufValidationStatus::NotYetValidated,   GgufValidationStatus::PathNotFound,
            GgufValidationStatus::PathNotRegularFile, GgufValidationStatus::PathUnreadable,
            GgufValidationStatus::FileEmpty,          GgufValidationStatus::InvalidMagic,
            GgufValidationStatus::UnsupportedVersion, GgufValidationStatus::TruncatedHeader,
            GgufValidationStatus::MalformedMetadata,  GgufValidationStatus::Valid,
        };
        for (GgufValidationStatus s : all) {
            const char* msg = gguf_validation_status_message(s);
            check(msg != nullptr && msg[0] != '\0', "gguf_validation_status_message non-empty for every status");
        }
    }

    if (g_failures == 0) {
        std::printf("test_gguf_reader: all checks passed.\n");
        return 0;
    }
    std::fprintf(stderr, "test_gguf_reader: %d check(s) failed.\n", g_failures);
    return 1;
}
