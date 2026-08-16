#include "model/model_registry.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>

#include "model/model_metadata.h"
#include "model/model_verifier.h"

namespace syj::edgemind {

namespace {
constexpr const char* kMagicLine = "SYJ_EDGEMIND_MODEL_REGISTRY_V1";

// Every RegistryEntry string field is percent-encoded on write and decoded
// on read (same idea as URL percent-encoding, reused here rather than
// inventing a new scheme): a local file path, display name, or checksum
// could in principle contain '=', '\n', or other characters that would
// otherwise collide with this format's "key=value, blank line separates
// entries" structure. Only '%', '\n', '\r', and '=' need escaping to keep
// the format unambiguous; everything else passes through unchanged so a
// normal path/name is still human-readable in the raw file.
std::string percent_encode(const std::string& s) {
    std::ostringstream out;
    out << std::hex;
    for (unsigned char c : s) {
        if (c == '%' || c == '\n' || c == '\r' || c == '=') {
            out << '%' << std::uppercase << std::hex;
            out.width(2);
            out.fill('0');
            out << static_cast<int>(c);
            out << std::nouppercase;
        } else {
            out << static_cast<char>(c);
        }
    }
    return out.str();
}

bool percent_decode(const std::string& s, std::string* out) {
    out->clear();
    out->reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%') {
            if (i + 2 >= s.size()) return false;
            auto hex_val = [](char c, int& v) {
                if (c >= '0' && c <= '9') { v = c - '0'; return true; }
                if (c >= 'A' && c <= 'F') { v = 10 + (c - 'A'); return true; }
                if (c >= 'a' && c <= 'f') { v = 10 + (c - 'a'); return true; }
                return false;
            };
            int hi = 0, lo = 0;
            if (!hex_val(s[i + 1], hi) || !hex_val(s[i + 2], lo)) return false;
            out->push_back(static_cast<char>((hi << 4) | lo));
            i += 2;
        } else {
            out->push_back(s[i]);
        }
    }
    return true;
}

bool strict_parse_u64(const std::string& s, uint64_t* out) {
    if (s.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const unsigned long long v = std::strtoull(s.c_str(), &end, 10);
    if (end != s.c_str() + s.size() || errno == ERANGE) return false;
    *out = static_cast<uint64_t>(v);
    return true;
}

bool strict_parse_i64(const std::string& s, int64_t* out) {
    if (s.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    if (end != s.c_str() + s.size() || errno == ERANGE) return false;
    *out = static_cast<int64_t>(v);
    return true;
}

const char* kFieldOrder[] = {
    "model_id", "display_name", "local_path", "file_size_bytes", "format",
    "architecture", "quantization", "verification_status", "verified_at_unix", "expected_checksum_sha256",
};

// VerificationStatus is stored as its underlying int so the format doesn't
// depend on enumerator names; only the exact set of values this version of
// the enum defines is accepted on read — an out-of-range value is
// Corrupted, not silently clamped or defaulted.
bool status_to_int(VerificationStatus s, int* out) {
    *out = static_cast<int>(s);
    return true;
}
bool int_to_status(int v, VerificationStatus* out) {
    if (v < static_cast<int>(VerificationStatus::NotYetVerified) ||
        v > static_cast<int>(VerificationStatus::Verified)) {
        return false;
    }
    *out = static_cast<VerificationStatus>(v);
    return true;
}

bool entry_to_lines(const RegistryEntry& e, std::ostream& out) {
    int status_int = 0;
    status_to_int(e.verification_status, &status_int);
    out << "model_id=" << percent_encode(e.model_id) << "\n";
    out << "display_name=" << percent_encode(e.display_name) << "\n";
    out << "local_path=" << percent_encode(e.local_path) << "\n";
    out << "file_size_bytes=" << e.file_size_bytes << "\n";
    out << "format=" << percent_encode(e.format) << "\n";
    out << "architecture=" << percent_encode(e.architecture) << "\n";
    out << "quantization=" << percent_encode(e.quantization) << "\n";
    out << "verification_status=" << status_int << "\n";
    out << "verified_at_unix=" << e.verified_at_unix << "\n";
    out << "expected_checksum_sha256=" << percent_encode(e.expected_checksum_sha256) << "\n";
    return out.good();
}

// Parses exactly one entry from a field->value map that was collected
// between blank lines. Returns false (fail closed) on any missing field,
// malformed number, or unrecognized status value.
bool fields_to_entry(const std::map<std::string, std::string>& fields, RegistryEntry* out) {
    for (const char* key : kFieldOrder) {
        if (fields.find(key) == fields.end()) return false;
    }
    RegistryEntry e;
    if (!percent_decode(fields.at("model_id"), &e.model_id)) return false;
    if (!percent_decode(fields.at("display_name"), &e.display_name)) return false;
    if (!percent_decode(fields.at("local_path"), &e.local_path)) return false;
    if (!strict_parse_u64(fields.at("file_size_bytes"), &e.file_size_bytes)) return false;
    if (!percent_decode(fields.at("format"), &e.format)) return false;
    if (!percent_decode(fields.at("architecture"), &e.architecture)) return false;
    if (!percent_decode(fields.at("quantization"), &e.quantization)) return false;
    {
        int64_t status_i64 = 0;
        if (!strict_parse_i64(fields.at("verification_status"), &status_i64)) return false;
        if (status_i64 < INT32_MIN || status_i64 > INT32_MAX) return false;
        if (!int_to_status(static_cast<int>(status_i64), &e.verification_status)) return false;
    }
    if (!strict_parse_i64(fields.at("verified_at_unix"), &e.verified_at_unix)) return false;
    if (!percent_decode(fields.at("expected_checksum_sha256"), &e.expected_checksum_sha256)) return false;

    if (e.model_id.empty()) return false; // primary key must never be empty
    *out = e;
    return true;
}

} // namespace

RegistryLoadResult ModelRegistry::load(const std::string& registry_path, std::vector<RegistryEntry>* out_entries) {
    out_entries->clear();

    std::ifstream in(registry_path);
    if (!in.is_open()) {
        return RegistryLoadResult::NotFound;
    }

    std::string magic_line;
    if (!std::getline(in, magic_line) || magic_line != kMagicLine) {
        return RegistryLoadResult::Corrupted;
    }

    std::map<std::string, std::string> fields;
    std::string line;
    auto flush_entry = [&]() -> bool {
        if (fields.empty()) return true; // blank-line run, nothing to flush
        RegistryEntry entry;
        if (!fields_to_entry(fields, &entry)) return false;
        out_entries->push_back(entry);
        fields.clear();
        return true;
    };

    while (std::getline(in, line)) {
        if (line.empty()) {
            if (!flush_entry()) return RegistryLoadResult::Corrupted;
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0) {
            return RegistryLoadResult::Corrupted; // malformed line
        }
        const std::string key = line.substr(0, eq);
        bool known = false;
        for (const char* k : kFieldOrder) {
            if (key == k) { known = true; break; }
        }
        if (!known) return RegistryLoadResult::Corrupted; // unrecognized field — fail closed
        fields[key] = line.substr(eq + 1);
    }
    if (!flush_entry()) return RegistryLoadResult::Corrupted;

    return RegistryLoadResult::Ok;
}

bool ModelRegistry::save(const std::string& registry_path, const std::vector<RegistryEntry>& entries) {
    const std::string tmp_path = registry_path + ".tmp";
    {
        std::ofstream out(tmp_path, std::ios::trunc);
        if (!out.is_open()) return false;
        out << kMagicLine << "\n";
        for (const RegistryEntry& e : entries) {
            if (e.model_id.empty()) return false; // never persist an entry with no identity
            if (!entry_to_lines(e, out)) return false;
            out << "\n"; // blank line separates entries
        }
        out.flush();
        if (!out.good()) return false;
    } // closed before rename, required on some platforms

    if (std::rename(tmp_path.c_str(), registry_path.c_str()) != 0) {
        std::remove(tmp_path.c_str());
        return false;
    }
    return true;
}

VerificationResult ModelRegistry::import_model(const std::string& registry_path, const std::string& model_path,
                                                const std::string& expected_checksum_sha256, bool* out_was_new_entry) {
    if (out_was_new_entry) *out_was_new_entry = false;

    VerificationResult result = ModelVerifier::verify(model_path, expected_checksum_sha256);
    if (!result.identity.computed) {
        // No deterministic identity to key an entry on — see this
        // function's header comment. The registry is left untouched;
        // callers still get the full rejection detail via `result`.
        return result;
    }

    std::vector<RegistryEntry> entries;
    const RegistryLoadResult load_result = load(registry_path, &entries);
    if (load_result == RegistryLoadResult::Corrupted) {
        // Fail closed: do not attempt to append to (or silently replace) a
        // registry file we could not trust — same posture as
        // UsageStateStore treating Corrupted as distinct from NotFound.
        result.diagnostic += "\n\nWARNING: local model registry at " + registry_path +
                              " is corrupted; import was verified but NOT recorded.\n" +
                              "See docs/model-registry.md's \"Known limitations\" for recovery guidance.";
        return result;
    }

    RegistryEntry entry;
    entry.model_id = result.identity.sha256_hex;
    entry.display_name = result.metadata.name_present ? result.metadata.name : model_path;
    entry.local_path = model_path;
    entry.file_size_bytes = 0; // filled in below via filesystem query
    entry.format = "gguf";
    entry.architecture = result.metadata.architecture_present ? result.metadata.architecture : std::string();
    entry.quantization = result.metadata.file_type_present ? model_ftype_name(result.metadata.file_type) : std::string();
    entry.verification_status = result.status;
    entry.expected_checksum_sha256 = expected_checksum_sha256;

    {
        std::ifstream size_probe(model_path, std::ios::binary | std::ios::ate);
        if (size_probe.is_open()) {
            const std::streampos size = size_probe.tellg();
            if (size >= 0) entry.file_size_bytes = static_cast<uint64_t>(size);
        }
    }

    // verified_at_unix is a real wall-clock timestamp — deliberately using
    // std::time() directly here (matching UsageManager's default injectable
    // clock) rather than plumbing a clock through every registry call;
    // unlike UsageAccounting's usage-window math, nothing here depends on
    // testing exact reset-boundary behavior, only "some plausible-looking
    // timestamp was recorded", so an injectable clock would add API surface
    // without a real testing need.
    entry.verified_at_unix = static_cast<int64_t>(std::time(nullptr));

    bool found_existing = false;
    for (RegistryEntry& existing : entries) {
        if (existing.model_id == entry.model_id) {
            existing = entry; // update in place — same identity, possibly moved/renamed
            found_existing = true;
            break;
        }
    }
    if (!found_existing) {
        entries.push_back(entry);
        if (out_was_new_entry) *out_was_new_entry = true;
    }

    if (!save(registry_path, entries)) {
        result.diagnostic += "\n\nWARNING: verification succeeded but the registry could not be saved to " +
                              registry_path + ".";
    }

    return result;
}

bool ModelRegistry::find_by_id(const std::string& registry_path, const std::string& model_id, RegistryEntry* out_entry) {
    std::vector<RegistryEntry> entries;
    if (load(registry_path, &entries) != RegistryLoadResult::Ok) {
        return false;
    }
    for (const RegistryEntry& e : entries) {
        if (e.model_id == model_id) {
            *out_entry = e;
            return true;
        }
    }
    return false;
}

} // namespace syj::edgemind
