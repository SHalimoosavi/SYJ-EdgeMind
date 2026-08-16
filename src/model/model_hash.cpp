#include "model/model_hash.h"

#include <array>
#include <cstring>
#include <fstream>
#include <vector>

namespace syj::edgemind {

namespace {

// Round constants: the first 32 bits of the fractional parts of the cube
// roots of the first 64 prime numbers (FIPS 180-4 §4.2.2).
constexpr uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

} // namespace

Sha256::Sha256() {
    // Initial hash values: the first 32 bits of the fractional parts of the
    // square roots of the first 8 prime numbers (FIPS 180-4 §5.3.3).
    state_[0] = 0x6a09e667;
    state_[1] = 0xbb67ae85;
    state_[2] = 0x3c6ef372;
    state_[3] = 0xa54ff53a;
    state_[4] = 0x510e527f;
    state_[5] = 0x9b05688c;
    state_[6] = 0x1f83d9ab;
    state_[7] = 0x5be0cd19;
    std::memset(buffer_, 0, sizeof(buffer_));
}

void Sha256::process_block(const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) | (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | static_cast<uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];

    for (int i = 0; i < 64; ++i) {
        const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t temp1 = h + S1 + ch + kK[i] + w[i];
        const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = S0 + maj;

        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
}

void Sha256::update(const uint8_t* data, size_t len) {
    bit_len_ += static_cast<uint64_t>(len) * 8;

    size_t offset = 0;
    if (buffer_len_ > 0) {
        const size_t need = 64 - buffer_len_;
        const size_t take = (len < need) ? len : need;
        std::memcpy(buffer_ + buffer_len_, data, take);
        buffer_len_ += take;
        offset += take;
        if (buffer_len_ == 64) {
            process_block(buffer_);
            buffer_len_ = 0;
        }
    }

    while (offset + 64 <= len) {
        process_block(data + offset);
        offset += 64;
    }

    if (offset < len) {
        const size_t remaining = len - offset;
        std::memcpy(buffer_, data + offset, remaining);
        buffer_len_ = remaining;
    }
}

std::string Sha256::finalize_hex() {
    // Padding per FIPS 180-4 §5.1.1: append 0x80, then zero bytes until the
    // length is congruent to 56 mod 64, then the original bit length as a
    // 64-bit big-endian integer.
    const uint64_t original_bit_len = bit_len_;

    uint8_t pad_byte = 0x80;
    update(&pad_byte, 1);
    // update() just added 8 bits to bit_len_ we don't want counted in the
    // length field — restore it before appending the real length below.
    bit_len_ = original_bit_len;

    uint8_t zero = 0x00;
    while (buffer_len_ != 56) {
        update(&zero, 1);
        bit_len_ = original_bit_len; // same correction each iteration
    }

    uint8_t len_bytes[8];
    for (int i = 0; i < 8; ++i) {
        len_bytes[i] = static_cast<uint8_t>(original_bit_len >> (56 - 8 * i));
    }
    // Append directly to the block buffer and process — bypass update()'s
    // bit_len_ accounting entirely since the length field itself must not
    // alter the length it records.
    std::memcpy(buffer_ + buffer_len_, len_bytes, 8);
    buffer_len_ += 8;
    process_block(buffer_);
    buffer_len_ = 0;

    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.resize(64);
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 4; ++j) {
            const uint8_t byte = static_cast<uint8_t>(state_[i] >> (24 - 8 * j));
            out[static_cast<size_t>(i) * 8 + static_cast<size_t>(j) * 2] = kHex[byte >> 4];
            out[static_cast<size_t>(i) * 8 + static_cast<size_t>(j) * 2 + 1] = kHex[byte & 0x0F];
        }
    }
    return out;
}

ModelIdentity compute_model_identity(const std::string& path) {
    ModelIdentity identity;

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return identity; // computed == false
    }

    Sha256 hasher;
    // Fixed, bounded chunk buffer regardless of file size — a multi-GB
    // model is hashed in 1 MB increments, never loaded whole into memory.
    std::vector<uint8_t> chunk(1024 * 1024);
    while (in) {
        in.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
        const std::streamsize got = in.gcount();
        if (got > 0) {
            hasher.update(chunk.data(), static_cast<size_t>(got));
        }
        if (in.eof()) {
            break;
        }
        if (in.fail()) {
            return identity; // computed == false — read error mid-file
        }
    }

    identity.sha256_hex = hasher.finalize_hex();
    identity.computed = true;
    return identity;
}

} // namespace syj::edgemind
