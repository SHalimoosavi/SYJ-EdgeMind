#ifndef SYJ_EDGEMIND_MODEL_MODEL_REGISTRY_H
#define SYJ_EDGEMIND_MODEL_MODEL_REGISTRY_H

#include <string>
#include <vector>

#include "model/model_types.h"

namespace syj::edgemind {

// Whether a registry load found a usable (possibly empty) registry, found
// none yet (first run), or found one that could not be trusted. Mirrors
// UsageStateStore's NotFound/Corrupted split (see usage_state_store.h) —
// a corrupted registry must never be silently treated as "empty, start
// fresh": that would quietly discard a user's prior import/verification
// history rather than surfacing the problem.
enum class RegistryLoadResult {
    Ok,
    NotFound,
    Corrupted,
};

// A local, file-persisted list of RegistryEntry records — the durable
// record of which models have been imported/verified on this device (see
// docs/model-registry.md). The ONLY filesystem-touching file in src/model/
// besides gguf_reader.cpp/model_hash.cpp (which read model files
// themselves, not the registry file); ModelVerifier and GgufReader remain
// unaware this class exists, matching the pure/impure separation used by
// src/memory and src/usage.
class ModelRegistry {
public:
    // Loads all entries from `registry_path`. `out_entries` is always left
    // in a well-defined state: empty on NotFound, whatever could be
    // trusted (i.e. nothing) on Corrupted, the real list on Ok.
    static RegistryLoadResult load(const std::string& registry_path, std::vector<RegistryEntry>* out_entries);

    // Atomically persists `entries` (tmp-file-then-rename, same pattern as
    // UsageStateStore::save() — see that file's comment for why this is
    // safe against a crash mid-write on POSIX platforms).
    static bool save(const std::string& registry_path, const std::vector<RegistryEntry>& entries);

    // Verifies `model_path` (see ModelVerifier::verify) and, if the
    // resulting identity could be computed (true for VerificationStatus::
    // Verified and ::ChecksumMismatch — both require a fully-hashed file;
    // every earlier failure mode, e.g. InvalidMagic, never reaches hashing
    // and therefore has no deterministic identity to key an entry on),
    // adds a new entry or updates the existing entry for that identity.
    //
    // Duplicate behavior: importing a file whose content-hash already
    // matches an existing entry does NOT create a second entry — it
    // updates the existing one's local_path/display_name/verification
    // fields in place (the file may have been moved/renamed since the
    // last import) and reports `*out_was_new_entry = false`. A genuinely
    // new identity gets a new entry and `*out_was_new_entry = true`.
    //
    // Returns the full VerificationResult regardless of whether an entry
    // was written, so callers can report exactly what happened (including
    // outright rejections that never touch the registry at all).
    static VerificationResult import_model(const std::string& registry_path, const std::string& model_path,
                                            const std::string& expected_checksum_sha256, bool* out_was_new_entry);

    // Looks up a single entry by its model_id (sha256 hex, case-sensitive
    // — identities are always stored/compared lowercase). Returns false if
    // the registry couldn't be read or no entry matches.
    static bool find_by_id(const std::string& registry_path, const std::string& model_id, RegistryEntry* out_entry);
};

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_MODEL_MODEL_REGISTRY_H
