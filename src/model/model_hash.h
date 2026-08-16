#ifndef SYJ_EDGEMIND_MODEL_MODEL_HASH_H
#define SYJ_EDGEMIND_MODEL_MODEL_HASH_H

#include <cstdint>
#include <string>

#include "model/model_types.h"

namespace syj::edgemind {

// Self-contained SHA-256 (FIPS 180-4), implemented directly rather than
// pulling in a crypto dependency (OpenSSL, mbedTLS, etc.) — see
// docs/model-registry.md's "Model identity" section for the tradeoff this
// was weighed against before implementation, per the mission's explicit
// requirement to document (not silently make) that decision. SHA-256 is a
// standard, well-specified public algorithm; this is not an invented hash.
//
// Streaming interface (update() any number of times, then finalize()) so a
// multi-gigabyte model file can be hashed in fixed-size chunks rather than
// loaded into memory in one allocation — consistent with the project's
// low-RAM-device ethos (see docs/memory-model.md).
class Sha256 {
public:
    Sha256();

    void update(const uint8_t* data, size_t len);

    // Finalizes the hash and returns it as 64 lowercase hex characters.
    // Calling update() after finalize() is undefined — construct a new
    // Sha256 instance to hash something else.
    std::string finalize_hex();

private:
    uint32_t state_[8];
    uint64_t bit_len_ = 0;
    uint8_t buffer_[64];
    size_t buffer_len_ = 0;

    void process_block(const uint8_t block[64]);
};

// Hashes the full contents of `path`, streaming it in bounded-size chunks
// (never loading the whole file into memory at once). Returns
// ModelIdentity{computed=false} if the file could not be opened/read to
// completion — callers (ModelVerifier) treat that as a verification
// failure, never as "identity is simply unavailable, proceed anyway".
ModelIdentity compute_model_identity(const std::string& path);

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_MODEL_MODEL_HASH_H
