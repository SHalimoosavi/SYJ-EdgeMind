#ifndef SYJ_EDGEMIND_MODEL_MODEL_METADATA_H
#define SYJ_EDGEMIND_MODEL_MODEL_METADATA_H

#include <cstdint>
#include <string>

namespace syj::edgemind {

// Maps a GGUF `general.file_type` value to the human-readable label
// llama.cpp uses for the same value (its `llama_ftype` enum) — e.g. 15 ->
// "Q4_K_M". This module does NOT link llama.cpp; the integer values below
// were read directly from the real llama.cpp public header
// (https://github.com/ggml-org/llama.cpp/blob/master/include/llama.h,
// fetched 2026-08-16) and are reproduced here as SYJ EdgeMind's own literal
// constants, not sourced from a linked copy of llama_ftype. This is
// presentation-layer data (a label for CLI/registry display) — it is
// deliberately NOT used for any verification/admission decision, so a
// future drift between this table and llama.cpp's actual enum (e.g. a new
// quantization type added upstream) degrades gracefully to "Unknown
// (file_type=N)" rather than silently misclassifying a model as something
// it isn't.
//
// NOTE: the sandbox that produced this file could not confirm this table
// matches the EXACT pinned tag (b10375) byte-for-byte — only `master` was
// fetchable. If master and the pin have diverged on file_type values
// (unlikely for a stable, long-established enum, but not proven here),
// this table may be stale for a handful of newer quantization types. This
// is a presentation-only risk (see above), not a verification-correctness
// risk.
std::string model_ftype_name(uint32_t file_type);

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_MODEL_MODEL_METADATA_H
