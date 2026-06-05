// ============================================================
//  sha256d.h - Double SHA-256 for Bitcoin mining
// ============================================================
#pragma once
#include <Arduino.h>
#include <stdint.h>

namespace WroomMiner {

void sha256d(const uint8_t* data, size_t len, uint8_t* output);

struct Sha256Midstate {
    uint32_t state[8];      // SHA256 state after header bytes 0-63
    uint8_t  remainder[16]; // header bytes 64-79
};

void sha256d_prepare_midstate(const uint8_t* header80, Sha256Midstate& midstate);

enum class Sha256dBackend : uint8_t {
    SoftwareMidstate,
    HardwareMidstate,
};

void sha256d_set_backend(Sha256dBackend backend);
Sha256dBackend sha256d_backend();
const char* sha256d_backend_name();
bool sha256d_try_enable_hardware_midstate(const Sha256Midstate& midstate,
                                          uint32_t nonce,
                                          const uint8_t* expected);

// Returns true and writes the 32-byte SHA256d hash.
// Kept as bool so a future proven early-exit filter can return false.
bool IRAM_ATTR sha256d_with_midstate(const Sha256Midstate& midstate,
                                     uint32_t nonce,
                                     uint8_t* output);

} // namespace WroomMiner
