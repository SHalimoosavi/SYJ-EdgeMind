#ifndef SYJ_EDGEMIND_TESTS_MODEL_TEST_GGUF_FIXTURE_H
#define SYJ_EDGEMIND_TESTS_MODEL_TEST_GGUF_FIXTURE_H

// Test-only GGUF fixture builder. NOT part of src/ — gguf_reader.cpp does
// not depend on this file or know it exists.
//
// Constructs real, byte-for-byte spec-correct GGUF fixtures directly in
// C++ (rather than shelling out to a script) so the Phase 3 test suite
// stays a self-contained native binary — the same portability constraint
// (no assumption of Python/scripting tools on Android/Termux) that led
// tests/test_temp_dir.h to resolve paths without shelling out either.
//
// Byte layout matches the real GGUF spec
// (https://ggml-org-ggml.mintlify.app/formats/gguf, fetched and verified
// 2026-08-16 — same source cited in src/model/gguf_reader.cpp) and was
// first prototyped in Python against a real `gguf_reader.cpp` build before
// being ported here; every fixture shape below was manually exercised
// against the reader during development and produced the expected
// GgufValidationStatus for each case (see the Phase 3 engineering report
// for that manual run's output).

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace syj::edgemind::test {

namespace gguf_fixture_detail {

enum : int32_t {
    GGUF_TYPE_UINT32 = 4,
    GGUF_TYPE_STRING = 8,
    GGUF_TYPE_ARRAY = 9,
    GGUF_TYPE_UINT64 = 10,
};

inline void put_u32_le(std::string& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
inline void put_u64_le(std::string& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
inline void put_i32_le(std::string& out, int32_t v) { put_u32_le(out, static_cast<uint32_t>(v)); }

inline void put_gguf_string(std::string& out, const std::string& s) {
    put_u64_le(out, s.size());
    out += s;
}

inline void put_kv_string(std::string& out, const std::string& key, const std::string& value) {
    put_gguf_string(out, key);
    put_i32_le(out, GGUF_TYPE_STRING);
    put_gguf_string(out, value);
}
inline void put_kv_u32(std::string& out, const std::string& key, uint32_t value) {
    put_gguf_string(out, key);
    put_i32_le(out, GGUF_TYPE_UINT32);
    put_u32_le(out, value);
}
inline void put_kv_u64(std::string& out, const std::string& key, uint64_t value) {
    put_gguf_string(out, key);
    put_i32_le(out, GGUF_TYPE_UINT64);
    put_u64_le(out, value);
}
inline void put_kv_string_array(std::string& out, const std::string& key, const std::vector<std::string>& values) {
    put_gguf_string(out, key);
    put_i32_le(out, GGUF_TYPE_ARRAY);
    put_i32_le(out, GGUF_TYPE_STRING);
    put_u64_le(out, values.size());
    for (const auto& v : values) put_gguf_string(out, v);
}

} // namespace gguf_fixture_detail

// A minimal, structurally valid GGUF file: general.architecture="llama",
// general.name="test-model", general.quantization_version=2,
// general.file_type=15 (Q4_K_M), llama.context_length=2048, plus a small
// tokenizer.ggml.tokens string array — exercises every KV type this
// reader handles (string, uint32, uint64, string-array) in one fixture.
// tensor_count=0 (this reader never parses the tensor-info section, so a
// nonzero count with no matching tensor-info bytes would itself be
// "malformed" by a stricter reader — zero sidesteps that entirely, which
// is correct: GgufReader's documented contract is header+KV-section only).
inline std::string build_valid_gguf(uint32_t version = 3) {
    using namespace gguf_fixture_detail;
    std::string body;
    put_kv_string(body, "general.architecture", "llama");
    put_kv_string(body, "general.name", "test-model");
    put_kv_u32(body, "general.quantization_version", 2);
    put_kv_u32(body, "general.file_type", 15);
    put_kv_u64(body, "llama.context_length", 2048);
    put_kv_string_array(body, "tokenizer.ggml.tokens", {"a", "b", "c"});

    std::string out = "GGUF";
    put_u32_le(out, version);
    put_u64_le(out, 0);  // tensor_count
    put_u64_le(out, 6);  // metadata_kv_count — must match the 6 put_kv_* calls above
    out += body;
    return out;
}

// A GGUF file with no general.architecture key (and therefore no
// resolvable context_length either) — general.name and general.file_type
// still present, to confirm partial-metadata is handled field-by-field
// rather than all-or-nothing.
inline std::string build_no_architecture_gguf() {
    using namespace gguf_fixture_detail;
    std::string body;
    put_kv_string(body, "general.name", "no-arch-model");
    put_kv_u32(body, "general.file_type", 0); // F32

    std::string out = "GGUF";
    put_u32_le(out, 3);
    put_u64_le(out, 0);
    put_u64_le(out, 2);
    out += body;
    return out;
}

inline std::string build_invalid_magic() {
    std::string v = build_valid_gguf();
    v[0] = 'N'; v[1] = 'O'; v[2] = 'P'; v[3] = 'E';
    return v;
}

inline std::string build_unsupported_version() {
    using namespace gguf_fixture_detail;
    std::string body;
    put_kv_string(body, "general.architecture", "llama");
    std::string out = "GGUF";
    put_u32_le(out, 99); // not 2 or 3
    put_u64_le(out, 0);
    put_u64_le(out, 1);
    out += body;
    return out;
}

inline std::string build_truncated_header() {
    return build_valid_gguf().substr(0, 10); // fewer than 24 header bytes
}

inline std::string build_truncated_metadata() {
    std::string full = build_valid_gguf();
    return full.substr(0, full.size() - 20); // cut off mid-KV-section
}

// A KV entry whose declared string length is astronomically larger than
// the file itself — must be rejected via the bounds check, never attempted
// as an allocation.
inline std::string build_malformed_huge_string_len() {
    using namespace gguf_fixture_detail;
    std::string out = "GGUF";
    put_u32_le(out, 3);
    put_u64_le(out, 0);
    put_u64_le(out, 1);
    put_gguf_string(out, "general.name");
    put_i32_le(out, GGUF_TYPE_STRING);
    put_u64_le(out, 0xFFFFFFFFFFFFFFFFULL); // bogus length
    return out;
}

// Header alone claims an absurd metadata_kv_count with zero actual KV
// bytes following — must be rejected by the sane-ceiling check, not by
// attempting to loop that many times.
inline std::string build_malformed_huge_kv_count() {
    using namespace gguf_fixture_detail;
    std::string out = "GGUF";
    put_u32_le(out, 3);
    put_u64_le(out, 0);
    put_u64_le(out, 0xFFFFFFFFFFFFFFFFULL);
    return out;
}

inline std::string build_malformed_unknown_type() {
    using namespace gguf_fixture_detail;
    std::string out = "GGUF";
    put_u32_le(out, 3);
    put_u64_le(out, 0);
    put_u64_le(out, 1);
    put_gguf_string(out, "weird.key");
    put_i32_le(out, 999); // not a real gguf_type
    return out;
}

// Writes `bytes` to `path`, truncating any existing content. Returns false
// on any I/O failure (caller should treat that as a test infrastructure
// failure, not a reader bug).
inline bool write_fixture(const std::string& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    if (!bytes.empty()) {
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    return out.good();
}

} // namespace syj::edgemind::test

#endif // SYJ_EDGEMIND_TESTS_MODEL_TEST_GGUF_FIXTURE_H
