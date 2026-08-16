#include <cstdio>
#include <cstring>
#include <fstream>

#include "model/model_hash.h"
#include "../test_temp_dir.h"

using syj::edgemind::ModelIdentity;
using syj::edgemind::Sha256;
using syj::edgemind::compute_model_identity;

namespace {
int g_failures = 0;

void check(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++g_failures;
    }
}

// NIST/FIPS 180-4 known-answer test vectors — not derived from this
// implementation, so a bug that produces a self-consistent-but-wrong hash
// cannot pass silently.
std::string sha256_of(const char* input) {
    Sha256 h;
    h.update(reinterpret_cast<const uint8_t*>(input), std::strlen(input));
    return h.finalize_hex();
}

std::string temp_path(const char* suffix) {
    return syj::edgemind::test::writable_temp_dir() + "/syj_edgemind_test_hash_" + suffix + ".bin";
}
} // namespace

int main() {
    check(sha256_of("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "SHA-256(\"\") matches FIPS 180-4 known-answer vector");
    check(sha256_of("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA-256(\"abc\") matches FIPS 180-4 known-answer vector");
    check(sha256_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "SHA-256(two-block message) matches FIPS 180-4 known-answer vector");

    // One-million-'a' vector, exercised via chunked update() (12345 + 300000
    // + remainder bytes) rather than a single call, since real usage
    // (compute_model_identity) always streams in fixed-size chunks.
    {
        const std::string million(1000000, 'a');
        Sha256 h;
        h.update(reinterpret_cast<const uint8_t*>(million.data()), 12345);
        h.update(reinterpret_cast<const uint8_t*>(million.data()) + 12345, 300000);
        h.update(reinterpret_cast<const uint8_t*>(million.data()) + 312345, million.size() - 312345);
        check(h.finalize_hex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
              "SHA-256(one million 'a', chunked update) matches FIPS 180-4 known-answer vector");
    }

    // Deterministic identity for the SAME content hashed twice must match.
    {
        const std::string path_a = temp_path("dup_a");
        const std::string path_b = temp_path("dup_b");
        const std::string content = "SYJ EdgeMind deterministic identity fixture content, repeated 37 times. ";
        std::string full;
        for (int i = 0; i < 37; ++i) full += content;

        {
            std::ofstream fa(path_a, std::ios::binary | std::ios::trunc);
            fa << full;
        }
        {
            std::ofstream fb(path_b, std::ios::binary | std::ios::trunc);
            fb << full;
        }

        ModelIdentity ida = compute_model_identity(path_a);
        ModelIdentity idb = compute_model_identity(path_b);
        check(ida.computed && idb.computed, "identity computed for both byte-identical files");
        check(ida.sha256_hex == idb.sha256_hex, "identical content -> identical deterministic identity");
        check(ida.sha256_hex.size() == 64, "identity is 64 lowercase hex characters");

        // Different content -> different identity (sanity: not a constant).
        {
            std::ofstream fb2(path_b, std::ios::binary | std::ios::trunc);
            fb2 << full << "x"; // one extra byte
        }
        ModelIdentity idb2 = compute_model_identity(path_b);
        check(idb2.computed && idb2.sha256_hex != ida.sha256_hex,
              "different content -> different identity");

        std::remove(path_a.c_str());
        std::remove(path_b.c_str());
    }

    // Nonexistent file -> computed == false, fails closed rather than
    // returning some default/placeholder hash.
    {
        ModelIdentity id = compute_model_identity(temp_path("does_not_exist_xyz"));
        check(!id.computed, "identity of nonexistent file -> computed == false");
    }

    // Empty file has a well-defined identity (SHA-256 of zero bytes) —
    // distinct from "not computed".
    {
        const std::string path = temp_path("empty");
        { std::ofstream f(path, std::ios::binary | std::ios::trunc); }
        ModelIdentity id = compute_model_identity(path);
        check(id.computed && id.sha256_hex == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              "empty file -> SHA-256(\"\") identity, still computed == true");
        std::remove(path.c_str());
    }

    if (g_failures == 0) {
        std::printf("test_model_hash: all checks passed.\n");
        return 0;
    }
    std::fprintf(stderr, "test_model_hash: %d check(s) failed.\n", g_failures);
    return 1;
}
