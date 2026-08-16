#include "model/model_types.h"

namespace syj::edgemind {

const char* gguf_validation_status_message(GgufValidationStatus status) {
    switch (status) {
        case GgufValidationStatus::NotYetValidated:   return "Not yet validated.";
        case GgufValidationStatus::PathNotFound:      return "Model path does not exist.";
        case GgufValidationStatus::PathNotRegularFile: return "Model path is not a regular file.";
        case GgufValidationStatus::PathUnreadable:    return "Model path could not be read.";
        case GgufValidationStatus::FileEmpty:         return "Model file is empty.";
        case GgufValidationStatus::InvalidMagic:      return "File is not a GGUF file (invalid magic).";
        case GgufValidationStatus::UnsupportedVersion: return "GGUF version is not supported.";
        case GgufValidationStatus::TruncatedHeader:   return "GGUF header is truncated.";
        case GgufValidationStatus::MalformedMetadata: return "GGUF metadata is malformed or exceeds sane limits.";
        case GgufValidationStatus::Valid:              return "GGUF structure is valid.";
    }
    return "Unknown GGUF validation status.";
}

const char* verification_status_message(VerificationStatus status) {
    switch (status) {
        case VerificationStatus::NotYetVerified:      return "Not yet verified.";
        case VerificationStatus::PathNotFound:        return "Model path does not exist.";
        case VerificationStatus::PathNotRegularFile:  return "Model path is not a regular file.";
        case VerificationStatus::PathUnreadable:      return "Model path could not be read.";
        case VerificationStatus::FileEmpty:           return "Model file is empty.";
        case VerificationStatus::InvalidMagic:        return "File is not a GGUF file (invalid magic).";
        case VerificationStatus::UnsupportedVersion:  return "GGUF version is not supported.";
        case VerificationStatus::TruncatedHeader:     return "GGUF header is truncated.";
        case VerificationStatus::MalformedMetadata:   return "GGUF metadata is malformed or exceeds sane limits.";
        case VerificationStatus::ChecksumMismatch:    return "Computed checksum does not match the expected checksum.";
        case VerificationStatus::Verified:            return "Model verified successfully.";
    }
    return "Unknown verification status.";
}

bool is_verified(VerificationStatus status) {
    return status == VerificationStatus::Verified;
}

} // namespace syj::edgemind
