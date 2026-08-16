#include <cstdio>
#include <filesystem>
#include <fstream>

#include "model/model_hash.h"
#include "model/model_verifier.h"
#include "../test_temp_dir.h"
#include "test_gguf_fixture.h"

using syj::edgemind::ModelVerifier;
using syj::edgemind::VerificationResult;
using syj::edgemind::VerificationStatus;
using syj::edgemind::compute_model_identity;
using syj::edgemind::is_verified;

namespace {
int g_failures = 0;

void check(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++g_failures;
    }
}

std::string path_for(const char* suffix) {
    return syj::edgemind::test::writable_temp_dir() + "/syj_edgemind_test_verifier_" + suffix + ".gguf";
}
} // namespace

int main() {
    // --- Valid model, no expected checksum configured -> Verified ---
    {
        const std::string path = path_for("valid_no_checksum");
        syj::edgemind::test::write_fixture(path, syj::edgemind::test::build_valid_gguf());
        VerificationResult r = ModelVerifier::verify(path);
        check(r.status == VerificationStatus::Verified, "valid GGUF, no checksum -> Verified");
        check(is_verified(r.status), "is_verified() agrees");
        check(r.identity.computed, "identity computed for a verified model");
        check(r.identity.sha256_hex.size() == 64, "identity is 64 hex chars");
        check(r.metadata.architecture_present && r.metadata.architecture == "llama", "metadata carried through");
        check(!r.diagnostic.empty(), "diagnostic message present");
        std::remove(path.c_str());
    }

    // --- Valid model, correct checksum -> Verified ---
    {
        const std::string path = path_for("valid_checksum_ok");
        syj::edgemind::test::write_fixture(path, syj::edgemind::test::build_valid_gguf());
        const std::string real_hash = compute_model_identity(path).sha256_hex;
        VerificationResult r = ModelVerifier::verify(path, real_hash);
        check(r.status == VerificationStatus::Verified, "valid GGUF, correct checksum -> Verified");

        // Uppercase-hex checksum must still match (case-insensitive compare).
        std::string upper = real_hash;
        for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        VerificationResult r2 = ModelVerifier::verify(path, upper);
        check(r2.status == VerificationStatus::Verified, "uppercase checksum still matches (case-insensitive)");
        std::remove(path.c_str());
    }

    // --- Valid model, wrong checksum -> ChecksumMismatch (not Verified,
    //     but metadata/identity still populated — "what we learned" vs
    //     "whether we accept it" are separate facts) ---
    {
        const std::string path = path_for("valid_checksum_bad");
        syj::edgemind::test::write_fixture(path, syj::edgemind::test::build_valid_gguf());
        VerificationResult r = ModelVerifier::verify(
            path, "0000000000000000000000000000000000000000000000000000000000000000");
        // (note: that literal is 68 chars, deliberately not matching any
        // real hash regardless of case-folding)
        check(r.status == VerificationStatus::ChecksumMismatch, "wrong checksum -> ChecksumMismatch");
        check(!is_verified(r.status), "is_verified() is false for a checksum mismatch");
        check(r.identity.computed, "identity is still populated on checksum mismatch");
        check(r.metadata.architecture_present, "metadata is still populated on checksum mismatch");
        std::remove(path.c_str());
    }

    // --- Directory instead of a file, even with a .gguf-looking name ---
    {
        const std::string path = path_for("a_directory");
        std::error_code ec;
        std::filesystem::create_directory(path, ec);
        check(!ec, "test setup: directory created");
        VerificationResult r = ModelVerifier::verify(path);
        check(r.status == VerificationStatus::PathNotRegularFile, "directory path -> PathNotRegularFile");
        std::filesystem::remove(path, ec);
    }

    // --- Nonexistent path ---
    {
        VerificationResult r = ModelVerifier::verify(path_for("does_not_exist_xyz"));
        check(r.status == VerificationStatus::PathNotFound, "nonexistent path -> PathNotFound");
    }

    // --- Empty file ---
    {
        const std::string path = path_for("empty");
        syj::edgemind::test::write_fixture(path, "");
        VerificationResult r = ModelVerifier::verify(path);
        check(r.status == VerificationStatus::FileEmpty, "empty file -> FileEmpty");
        std::remove(path.c_str());
    }

    // --- Invalid magic ---
    {
        const std::string path = path_for("bad_magic");
        syj::edgemind::test::write_fixture(path, syj::edgemind::test::build_invalid_magic());
        VerificationResult r = ModelVerifier::verify(path);
        check(r.status == VerificationStatus::InvalidMagic, "invalid magic -> InvalidMagic");
        check(!r.identity.computed, "identity NOT computed for a structurally-invalid file (no wasted hashing)");
        std::remove(path.c_str());
    }

    // --- Truncated / malformed metadata ---
    {
        const std::string path = path_for("trunc_meta");
        syj::edgemind::test::write_fixture(path, syj::edgemind::test::build_truncated_metadata());
        VerificationResult r = ModelVerifier::verify(path);
        check(r.status == VerificationStatus::MalformedMetadata, "truncated metadata -> MalformedMetadata");
        std::remove(path.c_str());
    }

    // --- Unsupported version ---
    {
        const std::string path = path_for("bad_version");
        syj::edgemind::test::write_fixture(path, syj::edgemind::test::build_unsupported_version());
        VerificationResult r = ModelVerifier::verify(path);
        check(r.status == VerificationStatus::UnsupportedVersion, "unsupported version -> UnsupportedVersion");
        std::remove(path.c_str());
    }

    // --- "Verification failure blocks inference": every non-Verified
    //     status must report is_verified() == false. This is the exact
    //     boolean Runtime::load() will gate on. ---
    {
        const VerificationStatus non_verified[] = {
            VerificationStatus::NotYetVerified,   VerificationStatus::PathNotFound,
            VerificationStatus::PathNotRegularFile, VerificationStatus::PathUnreadable,
            VerificationStatus::FileEmpty,        VerificationStatus::InvalidMagic,
            VerificationStatus::UnsupportedVersion, VerificationStatus::TruncatedHeader,
            VerificationStatus::MalformedMetadata, VerificationStatus::ChecksumMismatch,
        };
        for (VerificationStatus s : non_verified) {
            check(!is_verified(s), "every non-Verified status reports is_verified() == false");
        }
        check(is_verified(VerificationStatus::Verified), "Verified reports is_verified() == true");
    }

    if (g_failures == 0) {
        std::printf("test_model_verifier: all checks passed.\n");
        return 0;
    }
    std::fprintf(stderr, "test_model_verifier: %d check(s) failed.\n", g_failures);
    return 1;
}
