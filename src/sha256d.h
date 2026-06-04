// ============================================================
//  sha256d.h - Double SHA-256 for Bitcoin mining
//
//  Phase 1: software implementation via mbedTLS
//  Phase 3: switch to esp_sha (hardware acceleration)
//           + midstate optimization (NerdMiner trick)
// ============================================================
#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace WroomMiner {

// Computes SHA256(SHA256(data)).
//   input:  data, len bytes
//   output: 32-byte hash
void sha256d(const uint8_t* data, size_t len, uint8_t* output);

// Optimized variant specifically for the 80-byte Bitcoin block header.
// Uses the midstate trick: the first 64 bytes of the header do not change
// within a job - the SHA256 state after those 64 bytes can be precomputed
// and reused.
struct Sha256Midstate {
    uint32_t state[8];
    uint8_t  remainder[16]; // last 16 bytes of the header (nonce region)
};

// Computes the midstate for a 64-byte block.
// Called once per job by the Stratum client.
void sha256d_prepare_midstate(const uint8_t* header80, Sha256Midstate& midstate);

// Hash using a prepared midstate + nonce.
// ~5x faster than the naive variant. Output: 32 bytes.
void sha256d_with_midstate(const Sha256Midstate& midstate,
                           uint32_t nonce,
                           uint8_t* output);

} // namespace WroomMiner
