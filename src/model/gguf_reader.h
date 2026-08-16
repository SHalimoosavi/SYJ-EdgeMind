#ifndef SYJ_EDGEMIND_MODEL_GGUF_READER_H
#define SYJ_EDGEMIND_MODEL_GGUF_READER_H

#include <string>

#include "model/model_types.h"

namespace syj::edgemind {

// Parses a GGUF file's HEADER and METADATA KEY-VALUE section only — it
// never reads the tensor-info array or the tensor data blob (those come
// after the metadata section and, for a multi-gigabyte model, dwarf it in
// size; SYJ EdgeMind's verification step is deliberately cheap, not a full
// model load).
//
// Deliberately independent of llama.cpp: this parser implements the public
// GGUF binary format directly (header: magic/version/tensor_count/
// metadata_kv_count; then a sequence of typed key-value pairs), per the
// format documented at https://ggml-org-ggml.mintlify.app/formats/gguf
// (fetched and verified 2026-08-16 against the real spec — not assumed
// from memory). Doing this without llama.cpp means:
//   1. A malformed/adversarial file is rejected before llama.cpp's own
//      parser ever sees it (defense in depth — see docs/security.md).
//   2. This validator is unit-testable without linking llama.cpp at all,
//      matching the pure/impure split used throughout src/memory and
//      src/usage (see MemoryEstimator, UsageAccounting).
//
// FAIL-CLOSED: any length, count, or offset read from the file that would
// require reading past end-of-file, or that exceeds the sane ceilings in
// GgufLimits, immediately produces GgufValidationStatus::MalformedMetadata
// — never a best-effort partial parse presented as success.
class GgufReader {
public:
    // Reads `path` and returns the metadata this reader is able to extract
    // (see ModelMetadata), tagged with the validation status that resulted.
    // `out_metadata` is populated whenever parsing got at least as far as
    // a valid header (even if a later KV entry is malformed, whatever was
    // successfully parsed before that point is still returned — useful for
    // diagnostics, though callers must still check the returned status
    // before trusting the file for anything beyond display).
    //
    // Does NOT check path existence/regular-file-ness/readability/emptiness
    // — that is ModelVerifier's job (see model_verifier.h), which composes
    // this reader with those filesystem-level checks. This function assumes
    // it has already been handed an openable, non-empty path and reports
    // PathNotFound/PathUnreadable/FileEmpty only as a defensive fallback if
    // that assumption doesn't hold (e.g. a TOCTOU race), not as its primary
    // contract for those cases.
    static GgufValidationStatus validate(const std::string& path, ModelMetadata& out_metadata);
};

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_MODEL_GGUF_READER_H
