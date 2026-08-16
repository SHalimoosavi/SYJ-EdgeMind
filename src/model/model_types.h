#ifndef SYJ_EDGEMIND_MODEL_MODEL_TYPES_H
#define SYJ_EDGEMIND_MODEL_MODEL_TYPES_H

#include <cstdint>
#include <string>

namespace syj::edgemind {

// ---------------------------------------------------------------------------
// GGUF structural validation
// ---------------------------------------------------------------------------
//
// Deterministic, non-string-matching classification for every way a file
// can fail to be a usable GGUF model — mirrors the EngineError/UsageOutcome
// house style (see src/inference/inference_engine.h, src/usage/usage_types.h).
// This is the FIRST gate a model path passes through, before llama.cpp ever
// sees the file (see docs/model-registry.md for the full pipeline).
enum class GgufValidationStatus {
    NotYetValidated,
    PathNotFound,
    PathNotRegularFile,     // directory, symlink-to-nowhere, device file, etc.
    PathUnreadable,
    FileEmpty,
    InvalidMagic,           // first 4 bytes are not 'G' 'G' 'U' 'F'
    UnsupportedVersion,     // magic is correct but header.version isn't one
                             // this reader understands (see gguf_reader.cpp)
    TruncatedHeader,        // fewer than 24 header bytes present
    MalformedMetadata,      // a KV entry's declared length/type/count would
                             // read past EOF, or a count exceeds a sane
                             // ceiling — see SYJ_EDGEMIND_MAX_SANE_GGUF_* below
    Valid,
};

const char* gguf_validation_status_message(GgufValidationStatus status);

// Sanity ceilings applied while walking the GGUF metadata section. These
// exist for the same reason as MemoryEstimator's SYJ_EDGEMIND_MAX_SANE_*
// constants (see src/memory/memory_types.h): a corrupted or adversarial
// file can declare an arbitrary 64-bit length/count, and reading that value
// naively can allocate unbounded memory or attempt an unbounded read before
// any error becomes visible. These bounds are deliberately generous for any
// *legitimate* GGUF file (real files have at most a few thousand metadata
// entries and no single metadata string is remotely close to these sizes)
// while still being far below what would risk exhausting memory on a
// 4 GB-class target device.
struct GgufLimits {
    // Metadata key-value pair count. Real GGUF files (even large,
    // architecture-rich ones) have on the order of dozens to a few hundred
    // KV entries; tokenizer.ggml.tokens/scores/merges arrays account for
    // most of the actual data volume within a handful of KV *keys*.
    static constexpr uint64_t SYJ_EDGEMIND_MAX_SANE_KV_COUNT = 100'000;

    // Individual string length (metadata string value or KV key), in bytes.
    // The GGUF spec caps *keys* at 65535 bytes; string *values* (e.g.
    // general.description) have no spec-mandated cap, so this is SYJ
    // EdgeMind's own conservative ceiling, not a spec requirement.
    static constexpr uint64_t SYJ_EDGEMIND_MAX_SANE_STRING_BYTES = 16ULL * 1024 * 1024;

    // Array element count for a GGUF_TYPE_ARRAY value (e.g.
    // tokenizer.ggml.tokens). Real vocabularies run to a few hundred
    // thousand entries; this ceiling is generous above that.
    static constexpr uint64_t SYJ_EDGEMIND_MAX_SANE_ARRAY_COUNT = 10'000'000;

    // Tensor count (from the header only — SYJ EdgeMind's reader does not
    // walk the tensor-info section at all; see gguf_reader.h). Real models
    // have at most a few thousand tensors.
    static constexpr uint64_t SYJ_EDGEMIND_MAX_SANE_TENSOR_COUNT = 1'000'000;
};

// Metadata extracted from a GGUF file's key-value section WITHOUT ever
// calling into llama.cpp — see gguf_reader.h. Every field here has a
// concrete purpose: these are exactly the facts docs/model-registry.md
// documents as "inspectable pre-load", and each is used by either the CLI's
// /model-info display or the registry's duplicate-detection/display logic.
// Fields are populated only when the corresponding GGUF metadata KEY is
// present in the file; `*_present` flags distinguish "key absent" from
// "key present with a falsy/zero value" — the same "don't silently
// substitute a number for missing data" principle as
// MemoryEstimate::valid (see src/memory/memory_types.h).
struct ModelMetadata {
    // Header-level facts — always available once the header parses.
    uint32_t gguf_version = 0;
    uint64_t tensor_count = 0;
    uint64_t metadata_kv_count = 0;

    // general.architecture (string) — e.g. "llama", "gpt2". Required by the
    // GGUF spec, but SYJ EdgeMind does not assume it's present (a
    // malformed-but-not-crashing file could still omit it).
    bool architecture_present = false;
    std::string architecture;

    // general.name (string) — human-readable model name, for display only.
    bool name_present = false;
    std::string name;

    // general.quantization_version (uint32) — required by the spec when any
    // tensor is quantized; absent for f32/f16-only files.
    bool quantization_version_present = false;
    uint32_t quantization_version = 0;

    // general.file_type (uint32) — the dominant tensor quantization scheme,
    // encoded per llama.cpp's llama_ftype enum. Mapped to a human-readable
    // label by model_ftype_name() (model_metadata.cpp) using values
    // verified against the real llama.cpp header (see that file's comment
    // for the exact source/date) — SYJ EdgeMind does not link llama.cpp to
    // read this field, it only mirrors the enum's documented values.
    bool file_type_present = false;
    uint32_t file_type = 0;

    // [architecture].context_length (uint64) — e.g. "llama.context_length".
    // Read using whatever `architecture` resolved to; absent if
    // architecture itself is absent or the key isn't present.
    bool context_length_present = false;
    uint64_t context_length = 0;
};

// ---------------------------------------------------------------------------
// Model identity
// ---------------------------------------------------------------------------

// A model's deterministic identity: the lowercase-hex SHA-256 of the file's
// full byte contents. Two files with identical bytes always produce the
// same identity regardless of filename/path/mtime — this is what makes
// duplicate-import detection and "did this file change" checks meaningful
// (see docs/model-registry.md's "Model identity" section for why a
// filename or file size alone was rejected as an identity source).
struct ModelIdentity {
    bool computed = false;
    std::string sha256_hex; // 64 lowercase hex characters when computed == true
};

// ---------------------------------------------------------------------------
// Verification (the layer callers actually interact with)
// ---------------------------------------------------------------------------

// The end-to-end outcome of ModelVerifier::verify() — see model_verifier.h.
// This is the status Runtime::load() gates on before ever touching
// InferenceEngine/llama.cpp.
enum class VerificationStatus {
    NotYetVerified,
    PathNotFound,
    PathNotRegularFile,
    PathUnreadable,
    FileEmpty,
    InvalidMagic,
    UnsupportedVersion,
    TruncatedHeader,
    MalformedMetadata,
    ChecksumMismatch,   // GGUF structure was valid, but an expected checksum
                         // was configured and the computed one didn't match
    Verified,
};

const char* verification_status_message(VerificationStatus status);

// True only for VerificationStatus::Verified — the single gate every
// caller (Runtime::load(), the CLI's /verify, the C API) checks before
// proceeding to the next pipeline stage. Deliberately a function, not a
// property callers re-derive themselves, so "what counts as safe to
// proceed" has exactly one definition in the codebase.
bool is_verified(VerificationStatus status);

struct VerificationResult {
    VerificationStatus status = VerificationStatus::NotYetVerified;
    std::string diagnostic; // human-readable; STATUS: VERIFIED/REJECTED format,
                             // matching docs/memory-model.md's and
                             // docs/usage-model.md's diagnostic conventions
    ModelMetadata metadata; // populated whenever GGUF parsing got far enough
                             // to read the header + KV section, even if a
                             // later check (e.g. checksum) rejects the file —
                             // "what we learned" and "whether we accept it"
                             // are different facts, same as MemoryEstimate
                             // vs. MemoryDecision.
    ModelIdentity identity; // populated whenever the file could be fully
                             // read (identity requires hashing the whole
                             // file, not just the header/metadata region)
};

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

// One entry in the local model registry — see model_registry.h. Every field
// has a concrete purpose tied to a documented registry operation (list,
// lookup, duplicate detection, or CLI display); nothing here is speculative.
struct RegistryEntry {
    // sha256_hex of the file contents at import time — the entry's PRIMARY
    // KEY. Two imports of byte-identical files resolve to the same entry
    // (see ModelRegistry::import()'s documented duplicate behavior).
    std::string model_id;

    std::string display_name;   // metadata.name if present, else the
                                 // filename component of local_path
    std::string local_path;     // path used at the most recent successful
                                 // import/verify (may go stale if the user
                                 // moves the file — see docs/model-registry.md
                                 // "Known limitations")
    uint64_t file_size_bytes = 0;
    std::string format;         // fixed "gguf" today; a distinct field
                                 // rather than assuming GGUF forever, since
                                 // ROADMAP.md never rules out other formats
    std::string architecture;   // ModelMetadata::architecture, empty if
                                 // that key was absent
    std::string quantization;   // human-readable label from
                                 // model_ftype_name(), empty if
                                 // file_type_present was false

    VerificationStatus verification_status = VerificationStatus::NotYetVerified;
    int64_t verified_at_unix = 0; // 0 means "never verified"

    std::string expected_checksum_sha256; // empty means "none configured";
                                           // see ModelVerifier's optional
                                           // checksum-comparison behavior
};

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_MODEL_MODEL_TYPES_H
