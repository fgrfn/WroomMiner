// ============================================================
//  sha256d.cpp - SHA256d implementation (Phase 1: mbedTLS)
//
//  Phase 3 will switch this to esp_sha and add an optimized
//  midstate loop (target: 400+ KH/s).
// ============================================================
#include "sha256d.h"
#include <mbedtls/sha256.h>
#include <cstring>

namespace WroomMiner {

void sha256d(const uint8_t* data, size_t len, uint8_t* output) {
    uint8_t firstHash[32];

    // 1st round
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0); // 0 = SHA-256 (not 224)
    mbedtls_sha256_update(&ctx, data, len);
    mbedtls_sha256_finish(&ctx, firstHash);
    mbedtls_sha256_free(&ctx);

    // 2nd round - hash of the hash
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, firstHash, 32);
    mbedtls_sha256_finish(&ctx, output);
    mbedtls_sha256_free(&ctx);
}

void sha256d_prepare_midstate(const uint8_t* header80, Sha256Midstate& midstate) {
    // Compute the SHA256 state after the first 64 bytes of the header.
    // These 64 bytes (version + prevBlockHash + partial merkleRoot) do
    // not change within a job.
    //
    // TODO Phase 3: access mbedTLS's internal round functions directly.
    // For now we use the naive variant.
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, header80, 64);

    // Hack: read the internal state (mbedTLS internal API).
    // The internal field is .state[8] - this works with the ESP-IDF
    // standard build of mbedTLS.
    for (int i = 0; i < 8; ++i) {
        midstate.state[i] = ctx.state[i];
    }

    // Remaining 16 bytes (bytes 64-79: last merkleRoot part + ntime + nBits + nonce)
    memcpy(midstate.remainder, header80 + 64, 16);

    mbedtls_sha256_free(&ctx);
}

void sha256d_with_midstate(const Sha256Midstate& midstate,
                           uint32_t nonce,
                           uint8_t* output) {
    // Phase 1: naive fallback - we reconstruct the full 80-byte header
    // and hash it completely. Correct, but just as slow as calling
    // sha256d() directly.
    //
    // Phase 3 will implement the real midstate trick here:
    //   1. Load the precomputed state (start not at the IV)
    //   2. Process only the last 16 bytes + nonce + padding
    //   3. Run the second SHA256 as usual

    uint8_t header80[80];
    // We cannot reconstruct the first 64 bytes from the midstate -
    // in Phase 3 we won't need to. Phase 1 workaround: the caller
    // must keep the full header itself and use sha256d() directly.
    memset(header80, 0, 64);
    memcpy(header80 + 64, midstate.remainder, 16);

    // Write the nonce into the last 4 bytes (little-endian)
    header80[76] = (nonce >> 0)  & 0xFF;
    header80[77] = (nonce >> 8)  & 0xFF;
    header80[78] = (nonce >> 16) & 0xFF;
    header80[79] = (nonce >> 24) & 0xFF;

    sha256d(header80, 80, output);
}

} // namespace WroomMiner
