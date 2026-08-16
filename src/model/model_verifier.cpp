#include "model/model_verifier.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "model/gguf_reader.h"
#include "model/model_hash.h"
#include "model/model_metadata.h"

namespace syj::edgemind {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

VerificationStatus map_gguf_status(GgufValidationStatus s) {
    switch (s) {
        case GgufValidationStatus::NotYetValidated:    return VerificationStatus::NotYetVerified;
        case GgufValidationStatus::PathNotFound:       return VerificationStatus::PathNotFound;
        case GgufValidationStatus::PathNotRegularFile: return VerificationStatus::PathNotRegularFile;
        case GgufValidationStatus::PathUnreadable:     return VerificationStatus::PathUnreadable;
        case GgufValidationStatus::FileEmpty:          return VerificationStatus::FileEmpty;
        case GgufValidationStatus::InvalidMagic:       return VerificationStatus::InvalidMagic;
        case GgufValidationStatus::UnsupportedVersion: return VerificationStatus::UnsupportedVersion;
        case GgufValidationStatus::TruncatedHeader:    return VerificationStatus::TruncatedHeader;
        case GgufValidationStatus::MalformedMetadata:  return VerificationStatus::MalformedMetadata;
        case GgufValidationStatus::Valid:              return VerificationStatus::Verified; // provisional; may be
                                                                                              // downgraded to
                                                                                              // ChecksumMismatch below
    }
    return VerificationStatus::MalformedMetadata; // unreachable if GgufValidationStatus is exhaustive above
}

std::string build_diagnostic(const VerificationResult& r, const std::string& path) {
    std::ostringstream oss;
    if (is_verified(r.status)) {
        oss << "STATUS: VERIFIED\n";
        oss << "Model: " << path << "\n";
        if (r.identity.computed) {
            oss << "Identity (sha256): " << r.identity.sha256_hex << "\n";
        }
        if (r.metadata.architecture_present) {
            oss << "Architecture: " << r.metadata.architecture << "\n";
        }
        if (r.metadata.file_type_present) {
            oss << "Quantization: " << model_ftype_name(r.metadata.file_type) << "\n";
        }
    } else {
        oss << "STATUS: REJECTED\n";
        oss << "Model: " << path << "\n";
        oss << "Reason: " << verification_status_message(r.status) << "\n";
    }
    return oss.str();
}

} // namespace

VerificationResult ModelVerifier::verify(const std::string& path, const std::string& expected_checksum_sha256) {
    VerificationResult result;

    std::error_code ec;
    const std::filesystem::path fs_path(path);

    // std::filesystem::status() follows symlinks — a symlink to a regular
    // file resolves to regular_file (accepted); a symlink to a directory,
    // to nowhere, or to a special file resolves to directory/not_found/
    // other (rejected below) — exactly the "reject directories, don't
    // trust filenames as identity" posture the mission requires, without
    // SYJ EdgeMind needing to special-case symlinks itself.
    const std::filesystem::file_status st = std::filesystem::status(fs_path, ec);
    if (ec || !std::filesystem::exists(st)) {
        result.status = VerificationStatus::PathNotFound;
        result.diagnostic = build_diagnostic(result, path);
        return result;
    }
    if (!std::filesystem::is_regular_file(st)) {
        result.status = VerificationStatus::PathNotRegularFile;
        result.diagnostic = build_diagnostic(result, path);
        return result;
    }

    // Confirm actual readability (permissions can deny read even for an
    // existing regular file) and non-emptiness before handing off to
    // GgufReader — these are filesystem-level facts GgufReader's contract
    // explicitly does not re-derive (see gguf_reader.h).
    {
        std::ifstream probe(path, std::ios::binary);
        if (!probe.is_open()) {
            result.status = VerificationStatus::PathUnreadable;
            result.diagnostic = build_diagnostic(result, path);
            return result;
        }
        probe.seekg(0, std::ios::end);
        const std::streampos size = probe.tellg();
        if (size < 0) {
            result.status = VerificationStatus::PathUnreadable;
            result.diagnostic = build_diagnostic(result, path);
            return result;
        }
        if (size == 0) {
            result.status = VerificationStatus::FileEmpty;
            result.diagnostic = build_diagnostic(result, path);
            return result;
        }
    }

    const GgufValidationStatus gguf_status = GgufReader::validate(path, result.metadata);
    result.status = map_gguf_status(gguf_status);
    if (!is_verified(result.status)) {
        result.diagnostic = build_diagnostic(result, path);
        return result;
    }

    // Only hash files that passed structural validation — see
    // model_verifier.h's class comment for why.
    result.identity = compute_model_identity(path);
    if (!result.identity.computed) {
        // Could not re-read the file to hash it after just having read it
        // to validate it (e.g. a TOCTOU race where the file was deleted or
        // permissions changed between the two opens). Fail closed rather
        // than reporting Verified with no identity.
        result.status = VerificationStatus::PathUnreadable;
        result.diagnostic = build_diagnostic(result, path);
        return result;
    }

    if (!expected_checksum_sha256.empty()) {
        if (to_lower(expected_checksum_sha256) != result.identity.sha256_hex) {
            result.status = VerificationStatus::ChecksumMismatch;
            result.diagnostic = build_diagnostic(result, path);
            return result;
        }
    }

    result.status = VerificationStatus::Verified;
    result.diagnostic = build_diagnostic(result, path);
    return result;
}

} // namespace syj::edgemind
