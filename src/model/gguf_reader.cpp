#include "model/gguf_reader.h"

#include <cstdint>
#include <fstream>
#include <optional>
#include <unordered_map>
#include <vector>

namespace syj::edgemind {

namespace {

// GGUF metadata value types — per the spec's `gguf_type` enum. All enums in
// the format are stored as int32_t. Values verified against
// https://ggml-org-ggml.mintlify.app/formats/gguf (fetched 2026-08-16).
enum class GgufType : int32_t {
    UINT8   = 0,
    INT8    = 1,
    UINT16  = 2,
    INT16   = 3,
    UINT32  = 4,
    INT32   = 5,
    FLOAT32 = 6,
    BOOL    = 7,
    STRING  = 8,
    ARRAY   = 9,
    UINT64  = 10,
    INT64   = 11,
    FLOAT64 = 12,
};

// Fixed byte width of a scalar GGUF type. Returns 0 for STRING/ARRAY, which
// have no fixed width (callers must special-case those).
size_t fixed_scalar_width(GgufType t) {
    switch (t) {
        case GgufType::UINT8:
        case GgufType::INT8:
        case GgufType::BOOL:
            return 1;
        case GgufType::UINT16:
        case GgufType::INT16:
            return 2;
        case GgufType::UINT32:
        case GgufType::INT32:
        case GgufType::FLOAT32:
            return 4;
        case GgufType::UINT64:
        case GgufType::INT64:
        case GgufType::FLOAT64:
            return 8;
        default:
            return 0;
    }
}

bool is_known_gguf_type(int32_t raw) {
    return raw >= static_cast<int32_t>(GgufType::UINT8) && raw <= static_cast<int32_t>(GgufType::FLOAT64);
}

// Minimal bounded binary reader over an already-open ifstream. Every read
// checks the stream's post-read state; a short read (including hitting
// EOF mid-field) is reported via the returned bool, never silently
// zero-filled. This is the enforcement point for "never read past EOF"
// regardless of what any declared length/count field claims.
class BoundedReader {
public:
    explicit BoundedReader(std::ifstream& in, uint64_t file_size) : in_(in), file_size_(file_size) {}

    bool read_bytes(char* dst, size_t n) {
        if (n == 0) {
            return true;
        }
        in_.read(dst, static_cast<std::streamsize>(n));
        return in_.good() || (in_.eof() && static_cast<size_t>(in_.gcount()) == n);
    }

    bool read_u8(uint8_t& out) {
        char b;
        if (!read_bytes(&b, 1)) return false;
        out = static_cast<uint8_t>(b);
        return true;
    }

    bool read_u16_le(uint16_t& out) {
        uint8_t b[2];
        if (!read_bytes(reinterpret_cast<char*>(b), 2)) return false;
        out = static_cast<uint16_t>(b[0]) | (static_cast<uint16_t>(b[1]) << 8);
        return true;
    }

    bool read_u32_le(uint32_t& out) {
        uint8_t b[4];
        if (!read_bytes(reinterpret_cast<char*>(b), 4)) return false;
        out = static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
              (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
        return true;
    }

    bool read_u64_le(uint64_t& out) {
        uint8_t b[8];
        if (!read_bytes(reinterpret_cast<char*>(b), 8)) return false;
        out = 0;
        for (int i = 7; i >= 0; --i) {
            out = (out << 8) | b[i];
        }
        return true;
    }

    bool read_i32_le(int32_t& out) {
        uint32_t u;
        if (!read_u32_le(u)) return false;
        out = static_cast<int32_t>(u);
        return true;
    }

    // Skips `n` bytes forward without buffering them. Fails (returns false)
    // if that would move past end-of-file — this is the check that turns
    // "declared length reaches past EOF" into MalformedMetadata rather than
    // an unbounded/undefined read.
    bool skip(uint64_t n) {
        const std::streampos cur = in_.tellg();
        if (cur < 0) return false;
        const uint64_t cur_pos = static_cast<uint64_t>(cur);
        if (n > file_size_ || cur_pos > file_size_ - n) {
            return false; // would read past EOF
        }
        in_.seekg(static_cast<std::streamoff>(n), std::ios::cur);
        return in_.good();
    }

    // Reads a bounded string's raw bytes into `out`. `max_len` is the
    // caller's sanity ceiling (GgufLimits); a declared length beyond either
    // that ceiling or the remaining file size is rejected before any
    // allocation is attempted.
    bool read_string_bounded(uint64_t len, uint64_t max_len, std::string& out) {
        if (len > max_len) return false;
        const std::streampos cur = in_.tellg();
        if (cur < 0) return false;
        const uint64_t cur_pos = static_cast<uint64_t>(cur);
        if (len > file_size_ || cur_pos > file_size_ - len) return false;
        out.resize(static_cast<size_t>(len));
        if (len == 0) return true;
        return read_bytes(out.data(), static_cast<size_t>(len));
    }

private:
    std::ifstream& in_;
    uint64_t file_size_;
};

// Reads a GGUF string value (u64 length + UTF-8 bytes). If `capture` is
// true and the length is within GgufLimits, the bytes are read into
// `out_value`; otherwise the bytes are skipped without ever being
// allocated/copied — this is what keeps this parser's memory use bounded
// regardless of how many uninteresting string keys/values a file contains.
bool consume_string_value(BoundedReader& r, bool capture, std::string* out_value) {
    uint64_t len = 0;
    if (!r.read_u64_le(len)) return false;
    if (len > GgufLimits::SYJ_EDGEMIND_MAX_SANE_STRING_BYTES) return false;
    if (capture && out_value != nullptr) {
        return r.read_string_bounded(len, GgufLimits::SYJ_EDGEMIND_MAX_SANE_STRING_BYTES, *out_value);
    }
    return r.skip(len);
}

// Consumes one scalar (non-array) GGUF value of the given type, optionally
// capturing it as a uint64 (for UINT32/UINT64 numeric keys we care about —
// general.quantization_version, general.file_type, [arch].context_length)
// or as a string (general.architecture, general.name). Returns false on
// any malformed/truncated read.
bool consume_scalar_value(BoundedReader& r, GgufType type, bool capture_numeric, uint64_t* out_numeric,
                           bool capture_string, std::string* out_string) {
    if (type == GgufType::STRING) {
        return consume_string_value(r, capture_string, out_string);
    }
    const size_t width = fixed_scalar_width(type);
    if (width == 0) {
        return false; // ARRAY is handled by the caller, anything else is unknown
    }
    if (!capture_numeric) {
        return r.skip(width);
    }
    switch (type) {
        case GgufType::UINT8:
        case GgufType::INT8:
        case GgufType::BOOL: {
            uint8_t v;
            if (!r.read_u8(v)) return false;
            if (out_numeric) *out_numeric = v;
            return true;
        }
        case GgufType::UINT16:
        case GgufType::INT16: {
            uint16_t v;
            if (!r.read_u16_le(v)) return false;
            if (out_numeric) *out_numeric = v;
            return true;
        }
        case GgufType::UINT32:
        case GgufType::INT32: {
            uint32_t v;
            if (!r.read_u32_le(v)) return false;
            if (out_numeric) *out_numeric = v;
            return true;
        }
        case GgufType::FLOAT32:
            return r.skip(4); // never a key we numerically capture
        case GgufType::UINT64:
        case GgufType::INT64: {
            uint64_t v;
            if (!r.read_u64_le(v)) return false;
            if (out_numeric) *out_numeric = v;
            return true;
        }
        case GgufType::FLOAT64:
            return r.skip(8);
        default:
            return false;
    }
}

// Consumes one full metadata VALUE (scalar or array-of-scalar/array-of-string)
// for a key whose name we've already decided we do/don't care about.
// Nested arrays (array-of-array) are rejected as MalformedMetadata — the
// GGUF spec does not document that shape and no real writer produces it;
// accepting it would mean recursive, harder-to-bound parsing for zero
// practical benefit.
bool consume_value(BoundedReader& r, GgufType type, bool capture_numeric, uint64_t* out_numeric,
                    bool capture_string, std::string* out_string) {
    if (type != GgufType::ARRAY) {
        return consume_scalar_value(r, type, capture_numeric, out_numeric, capture_string, out_string);
    }

    int32_t elem_type_raw = 0;
    if (!r.read_i32_le(elem_type_raw)) return false;
    if (!is_known_gguf_type(elem_type_raw)) return false;
    const GgufType elem_type = static_cast<GgufType>(elem_type_raw);
    if (elem_type == GgufType::ARRAY) return false; // nested array: unsupported, reject

    uint64_t count = 0;
    if (!r.read_u64_le(count)) return false;
    if (count > GgufLimits::SYJ_EDGEMIND_MAX_SANE_ARRAY_COUNT) return false;

    // Arrays are never a key we capture a value for (context_length etc.
    // are always scalars per the spec) — every element is skipped, and for
    // STRING elements each one still needs its own bounded length read
    // since string elements aren't fixed-width.
    if (elem_type == GgufType::STRING) {
        for (uint64_t i = 0; i < count; ++i) {
            if (!consume_string_value(r, /*capture=*/false, nullptr)) return false;
        }
        return true;
    }

    const size_t width = fixed_scalar_width(elem_type);
    if (width == 0) return false; // unreachable given the checks above
    // Overflow-checked total skip size: count and width are both bounded
    // above (count <= 10,000,000; width <= 8), so this cannot overflow
    // uint64_t, but the multiplication is written explicitly rather than
    // assumed safe, matching MemoryEstimator's overflow-checked-arithmetic
    // convention (see src/memory/memory_estimator.cpp).
    const uint64_t total_bytes = static_cast<uint64_t>(width) * count;
    return r.skip(total_bytes);
}

} // namespace

GgufValidationStatus GgufReader::validate(const std::string& path, ModelMetadata& out_metadata) {
    out_metadata = ModelMetadata{};

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        // Defensive fallback only — ModelVerifier is expected to have
        // already confirmed the path exists/is readable before calling
        // here (see gguf_reader.h's contract comment).
        return GgufValidationStatus::PathNotFound;
    }

    in.seekg(0, std::ios::end);
    const std::streampos end_pos = in.tellg();
    if (end_pos < 0) {
        return GgufValidationStatus::PathUnreadable;
    }
    const uint64_t file_size = static_cast<uint64_t>(end_pos);
    in.seekg(0, std::ios::beg);

    if (file_size == 0) {
        return GgufValidationStatus::FileEmpty;
    }
    if (file_size < 24) { // magic(4) + version(4) + tensor_count(8) + metadata_kv_count(8)
        return GgufValidationStatus::TruncatedHeader;
    }

    BoundedReader r(in, file_size);

    char magic[4];
    if (!r.read_bytes(magic, 4)) {
        return GgufValidationStatus::TruncatedHeader;
    }
    if (magic[0] != 'G' || magic[1] != 'G' || magic[2] != 'U' || magic[3] != 'F') {
        return GgufValidationStatus::InvalidMagic;
    }

    uint32_t version = 0;
    if (!r.read_u32_le(version)) {
        return GgufValidationStatus::TruncatedHeader;
    }
    // Version 1 used uint32_t tensor_count/metadata_kv_count fields (per
    // the format's own version history: "v2: most countable fields changed
    // from uint32 to uint64"). Parsing a v1 file with this reader's v2/v3
    // (uint64-field) layout would silently misread the header. Rather than
    // branch the whole parser to support a format that predates any GGUF
    // file SYJ EdgeMind's own llama.cpp pin would produce, v1 is reported
    // as UnsupportedVersion — an honest limitation, not a silent
    // misparse. v2 and v3 (current) both use uint64 fields identically for
    // everything this reader touches.
    if (version != 2 && version != 3) {
        return GgufValidationStatus::UnsupportedVersion;
    }

    uint64_t tensor_count = 0;
    uint64_t metadata_kv_count = 0;
    if (!r.read_u64_le(tensor_count) || !r.read_u64_le(metadata_kv_count)) {
        return GgufValidationStatus::TruncatedHeader;
    }
    if (tensor_count > GgufLimits::SYJ_EDGEMIND_MAX_SANE_TENSOR_COUNT ||
        metadata_kv_count > GgufLimits::SYJ_EDGEMIND_MAX_SANE_KV_COUNT) {
        return GgufValidationStatus::MalformedMetadata;
    }

    out_metadata.gguf_version = version;
    out_metadata.tensor_count = tensor_count;
    out_metadata.metadata_kv_count = metadata_kv_count;

    // Keys of a form "<architecture>.context_length" may appear before
    // general.architecture itself in file order (the spec does not mandate
    // an order), so numeric context_length candidates are collected by
    // their full key name here and resolved against `architecture` only
    // after the whole KV section has been walked.
    std::unordered_map<std::string, uint64_t> context_length_candidates;
    std::optional<uint64_t> quantization_version;
    std::optional<uint64_t> file_type;

    for (uint64_t i = 0; i < metadata_kv_count; ++i) {
        uint64_t key_len = 0;
        if (!r.read_u64_le(key_len)) return GgufValidationStatus::MalformedMetadata;
        // Per spec, keys are "at most 65535 bytes" — enforced as a tighter,
        // spec-derived ceiling than the general string-value ceiling.
        std::string key;
        if (!r.read_string_bounded(key_len, 65535, key)) {
            return GgufValidationStatus::MalformedMetadata;
        }

        int32_t type_raw = 0;
        if (!r.read_i32_le(type_raw)) return GgufValidationStatus::MalformedMetadata;
        if (!is_known_gguf_type(type_raw) && type_raw != static_cast<int32_t>(GgufType::ARRAY)) {
            return GgufValidationStatus::MalformedMetadata;
        }
        const GgufType type = static_cast<GgufType>(type_raw);

        const bool want_architecture = (key == "general.architecture" && type == GgufType::STRING);
        const bool want_name = (key == "general.name" && type == GgufType::STRING);
        const bool want_quant_version = (key == "general.quantization_version" &&
                                          (type == GgufType::UINT32 || type == GgufType::INT32));
        const bool want_file_type = (key == "general.file_type" &&
                                      (type == GgufType::UINT32 || type == GgufType::INT32));
        const bool is_context_length_key = key.size() > std::string(".context_length").size() &&
                                            key.rfind(".context_length") == key.size() - std::string(".context_length").size() &&
                                            (type == GgufType::UINT32 || type == GgufType::UINT64 ||
                                             type == GgufType::INT32 || type == GgufType::INT64);

        std::string captured_string;
        uint64_t captured_numeric = 0;
        const bool capture_string = want_architecture || want_name;
        const bool capture_numeric = want_quant_version || want_file_type || is_context_length_key;

        if (!consume_value(r, type, capture_numeric, &captured_numeric, capture_string, &captured_string)) {
            return GgufValidationStatus::MalformedMetadata;
        }

        if (want_architecture) {
            out_metadata.architecture = captured_string;
            out_metadata.architecture_present = true;
        } else if (want_name) {
            out_metadata.name = captured_string;
            out_metadata.name_present = true;
        } else if (want_quant_version) {
            quantization_version = captured_numeric;
        } else if (want_file_type) {
            file_type = captured_numeric;
        } else if (is_context_length_key) {
            const std::string prefix = key.substr(0, key.size() - std::string(".context_length").size());
            context_length_candidates[prefix] = captured_numeric;
        }
    }

    if (quantization_version.has_value()) {
        out_metadata.quantization_version = static_cast<uint32_t>(*quantization_version);
        out_metadata.quantization_version_present = true;
    }
    if (file_type.has_value()) {
        out_metadata.file_type = static_cast<uint32_t>(*file_type);
        out_metadata.file_type_present = true;
    }
    if (out_metadata.architecture_present) {
        const auto it = context_length_candidates.find(out_metadata.architecture);
        if (it != context_length_candidates.end()) {
            out_metadata.context_length = it->second;
            out_metadata.context_length_present = true;
        }
    }

    return GgufValidationStatus::Valid;
}

} // namespace syj::edgemind
