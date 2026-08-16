#ifndef SYJ_EDGEMIND_MODEL_MODEL_VERIFIER_H
#define SYJ_EDGEMIND_MODEL_MODEL_VERIFIER_H

#include <string>

#include "model/model_types.h"

namespace syj::edgemind {

// The single entry point every caller (Runtime::load(), the CLI's
// /verify and /import, the C API) uses to decide whether a local file is
// safe to hand to InferenceEngine/llama.cpp. Composes, in order:
//   1. Filesystem checks: exists, is a regular file (not a directory or
//      special file), is readable, is non-empty.
//   2. GgufReader::validate() — structural GGUF header/metadata validation,
//      entirely independent of llama.cpp (see gguf_reader.h).
//   3. compute_model_identity() — SHA-256 of the full file, only attempted
//      once step 2 has confirmed the file is structurally valid (hashing a
//      file already known to be malformed/rejected wastes the I/O for no
//      benefit — nothing downstream needs the identity of a rejected file).
//   4. If an expected checksum was configured, compares it against the
//      computed identity.
//
// FAIL-CLOSED at every step: any failure short-circuits immediately with
// the specific VerificationStatus that explains it — there is no path that
// reaches VerificationStatus::Verified without every step above having
// explicitly succeeded.
class ModelVerifier {
public:
    // `expected_checksum_sha256` is optional — pass an empty string to skip
    // checksum comparison entirely (this is the common case: most local
    // imports have no separately-obtained checksum to compare against, and
    // omitting one is not itself a failure — GGUF structural validity is
    // still enforced regardless of whether a checksum is configured).
    // Comparison is case-insensitive (a user may paste an uppercase-hex
    // checksum from elsewhere); computed identity is always lowercase.
    static VerificationResult verify(const std::string& path,
                                      const std::string& expected_checksum_sha256 = std::string());
};

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_MODEL_MODEL_VERIFIER_H
