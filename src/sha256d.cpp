// ============================================================
//  sha256d.cpp  —  SHA256d for Bitcoin mining
//
//  Midstate approach inspired by NerdMiner_v2 (BitMaker-hub/NerdMiner_v2),
//  originally based on Blockstream Jade shaLib.
//  License: MIT  https://github.com/BitMaker-hub/NerdMiner_v2
//
//  Key design:
//  - Computes the midstate from header bytes 0-63 once/job.
//  - sha256d_with_midstate(): resumes from midstate, processes only
//    the 16-byte tail + nonce, runs second SHA256 pass.
//  - IRAM_ATTR: avoids flash cache misses on the hot path.
// ============================================================
#include "sha256d.h"
#include <mbedtls/sha256.h>
#include <sha/sha_parallel_engine.h>
#if CONFIG_IDF_TARGET_ESP32
#include <soc/hwcrypto_reg.h>
#include <soc/dport_reg.h>
#include <soc/dport_access.h>
#include <hal/sha_ll.h>
#endif
#include <cstring>

namespace WroomMiner {

#define ROTR(x, n) ((x >> n) | (x << ((sizeof(x) << 3) - n)))
#define SHR(x, n)  ((x & 0xFFFFFFFFu) >> n)

#define S0(x) (ROTR(x,  7) ^ ROTR(x, 18) ^ SHR(x,  3))
#define S1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ SHR(x, 10))
#define S2(x) (ROTR(x,  2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define S3(x) (ROTR(x,  6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define F0(x,y,z) ((x & y) | (z & (x | y)))
#define F1(x,y,z) (z ^ (x & (y ^ z)))

#define GET_BE(b,i) \
    (((uint32_t)(b)[(i)]<<24)|((uint32_t)(b)[(i)+1]<<16)| \
     ((uint32_t)(b)[(i)+2]<<8)|(uint32_t)(b)[(i)+3])

// Message schedule expansion
#define R(W,t) (W[t] = S1(W[t-2]) + W[t-7] + S0(W[t-15]) + W[t-16])

DRAM_ATTR static const uint32_t K[64] = {
    0x428A2F98,0x71374491,0xB5C0FBCF,0xE9B5DBA5,
    0x3956C25B,0x59F111F1,0x923F82A4,0xAB1C5ED5,
    0xD807AA98,0x12835B01,0x243185BE,0x550C7DC3,
    0x72BE5D74,0x80DEB1FE,0x9BDC06A7,0xC19BF174,
    0xE49B69C1,0xEFBE4786,0x0FC19DC6,0x240CA1CC,
    0x2DE92C6F,0x4A7484AA,0x5CB0A9DC,0x76F988DA,
    0x983E5152,0xA831C66D,0xB00327C8,0xBF597FC7,
    0xC6E00BF3,0xD5A79147,0x06CA6351,0x14292967,
    0x27B70A85,0x2E1B2138,0x4D2C6DFC,0x53380D13,
    0x650A7354,0x766A0ABB,0x81C2C92E,0x92722C85,
    0xA2BFE8A1,0xA81A664B,0xC24B8B70,0xC76C51A3,
    0xD192E819,0xD6990624,0xF40E3585,0x106AA070,
    0x19A4C116,0x1E376C08,0x2748774C,0x34B0BCB5,
    0x391C0CB3,0x4ED8AA4A,0x5B9CCA4F,0x682E6FF3,
    0x748F82EE,0x78A5636F,0x84C87814,0x8CC70208,
    0x90BEFFFA,0xA4506CEB,0xBEF9A3F7,0xC67178F2,
};

static volatile Sha256dBackend g_backend = Sha256dBackend::SoftwareMidstate;
static bool g_hwSwapMidstate = false;
static bool g_hwReverseDigestWords = false;

void sha256d_set_backend(Sha256dBackend backend) {
#if CONFIG_IDF_TARGET_ESP32
    g_backend = backend;
#else
    g_backend = Sha256dBackend::SoftwareMidstate;
#endif
}

Sha256dBackend sha256d_backend() {
    return g_backend;
}

const char* sha256d_backend_name() {
    return g_backend == Sha256dBackend::HardwareMidstate
        ? "ESP32 HW midstate"
        : "fast SW midstate";
}

bool sha256d_try_enable_hardware_midstate(const Sha256Midstate& midstate,
                                          uint32_t nonce,
                                          const uint8_t* expected) {
#if CONFIG_IDF_TARGET_ESP32
    uint8_t hash[32];
    for (int swap = 0; swap <= 1; ++swap) {
        for (int reverse = 0; reverse <= 1; ++reverse) {
            g_hwSwapMidstate = swap != 0;
            g_hwReverseDigestWords = reverse != 0;
            g_backend = Sha256dBackend::HardwareMidstate;
            if (sha256d_with_midstate(midstate, nonce, hash) &&
                memcmp(hash, expected, 32) == 0) {
                return true;
            }
        }
    }
#endif
    g_backend = Sha256dBackend::SoftwareMidstate;
    return false;
}

#if CONFIG_IDF_TARGET_ESP32
static inline void IRAM_ATTR sha_hw_wait_idle() {
    while (DPORT_REG_READ(SHA_256_BUSY_REG)) {
    }
}

static inline void IRAM_ATTR sha_hw_write_second_header_block(const uint8_t* tail,
                                                              uint32_t nonce) {
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(SHA_TEXT_BASE);
    const uint32_t* words = reinterpret_cast<const uint32_t*>(tail);

    reg[0] = words[0];
    reg[1] = words[1];
    reg[2] = words[2];
    reg[3] = __builtin_bswap32(nonce);
    reg[4] = 0x80000000u;
    reg[5] = 0;
    reg[6] = 0;
    reg[7] = 0;
    reg[8] = 0;
    reg[9] = 0;
    reg[10] = 0;
    reg[11] = 0;
    reg[12] = 0;
    reg[13] = 0;
    reg[14] = 0;
    reg[15] = 0x00000280u;
}

static inline void IRAM_ATTR sha_hw_write_second_sha_block() {
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(SHA_TEXT_BASE);

    // Words 0..7 already contain the first SHA digest after sha_ll_load().
    reg[8] = 0x80000000u;
    reg[9] = 0;
    reg[10] = 0;
    reg[11] = 0;
    reg[12] = 0;
    reg[13] = 0;
    reg[14] = 0;
    reg[15] = 0x00000100u;
}

static inline void IRAM_ATTR sha_hw_read_digest(uint8_t* out) {
    uint32_t* words = reinterpret_cast<uint32_t*>(out);

    DPORT_INTERRUPT_DISABLE();
    if (g_hwReverseDigestWords) {
        words[7] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 0 * 4));
        words[6] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 1 * 4));
        words[5] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 2 * 4));
        words[4] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 3 * 4));
        words[3] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 4 * 4));
        words[2] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 5 * 4));
        words[1] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 6 * 4));
        words[0] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 7 * 4));
    } else {
        words[0] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 0 * 4));
        words[1] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 1 * 4));
        words[2] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 2 * 4));
        words[3] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 3 * 4));
        words[4] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 4 * 4));
        words[5] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 5 * 4));
        words[6] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 6 * 4));
        words[7] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 7 * 4));
    }
    DPORT_INTERRUPT_RESTORE();
}

static bool IRAM_ATTR sha256d_hw_with_midstate(const Sha256Midstate& ms,
                                               uint32_t nonce,
                                               uint8_t* out) {
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(SHA_TEXT_BASE);

    DPORT_INTERRUPT_DISABLE();
    for (int i = 0; i < 8; ++i) {
        reg[i] = g_hwSwapMidstate ? __builtin_bswap32(ms.state[i]) : ms.state[i];
    }
    DPORT_INTERRUPT_RESTORE();

    // On classic ESP32 LOAD moves SHA_TEXT_BASE words into the SHA-256 state.
    sha_ll_load(SHA2_256);
    sha_hw_wait_idle();

    sha_hw_write_second_header_block(ms.remainder, nonce);
    sha_ll_continue_block(SHA2_256);
    sha_hw_wait_idle();

    // Move the first SHA digest back to SHA_TEXT_BASE, append padding, hash it.
    sha_ll_load(SHA2_256);
    sha_hw_wait_idle();
    sha_hw_write_second_sha_block();
    sha_ll_start_block(SHA2_256);
    sha_hw_wait_idle();
    sha_ll_load(SHA2_256);
    sha_hw_wait_idle();

    sha_hw_read_digest(out);
    return true;
}
#endif

// ============================================================
//  Generic SHA256d — mbedTLS (hardware-accelerated on ESP32)
//  Used for coinbase + merkle branch hashing.
// ============================================================
void sha256d(const uint8_t* data, size_t len, uint8_t* out) {
    uint8_t inner[32];
    esp_sha(SHA2_256, data, len, inner);
    esp_sha(SHA2_256, inner, sizeof(inner), out);
}

// ============================================================
//  Midstate: SHA256 state after compressing header bytes 0-63.
//  Called once per new job.
// ============================================================
void sha256d_prepare_midstate(const uint8_t* header80, Sha256Midstate& ms) {
    // Use mbedTLS to compress the first block; snapshot internal state.
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, header80, 64);
    for (int i = 0; i < 8; ++i) ms.state[i] = ctx.state[i];
    mbedtls_sha256_free(&ctx);

    // Save the 16-byte tail (header[64..79]): merkleRoot end + nTime + nBits + nonce.
    memcpy(ms.remainder, header80 + 64, 16);

}

// ============================================================
//  Hot path - resume SHA256 from the prepared first-block midstate.
// ============================================================
bool IRAM_ATTR sha256d_with_midstate(const Sha256Midstate& ms,
                                     uint32_t nonce,
                                     uint8_t* out) {
#if CONFIG_IDF_TARGET_ESP32
    if (g_backend == Sha256dBackend::HardwareMidstate) {
        return sha256d_hw_with_midstate(ms, nonce, out);
    }
#endif

    uint32_t W[64];
    W[0]  = GET_BE(ms.remainder, 0);
    W[1]  = GET_BE(ms.remainder, 4);
    W[2]  = GET_BE(ms.remainder, 8);
    // W[3] is header bytes 76-79. Bitcoin stores nonce little-endian.
    W[3]  = __builtin_bswap32(nonce);
    W[4]  = 0x80000000u;
    W[5]  = 0; W[6] = 0; W[7] = 0; W[8] = 0; W[9] = 0;
    W[10] = 0; W[11]= 0; W[12]= 0; W[13]= 0; W[14]= 0;
    W[15] = 0x00000280u;
    for (int t = 16; t < 64; ++t) {
        R(W, t);
    }

    uint32_t a = ms.state[0];
    uint32_t b = ms.state[1];
    uint32_t c = ms.state[2];
    uint32_t d = ms.state[3];
    uint32_t e = ms.state[4];
    uint32_t f = ms.state[5];
    uint32_t g = ms.state[6];
    uint32_t h = ms.state[7];

    for (int t = 0; t < 64; ++t) {
        uint32_t t1 = h + S3(e) + F1(e, f, g) + K[t] + W[t];
        uint32_t t2 = S2(a) + F0(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    uint32_t h0 = ms.state[0] + a;
    uint32_t h1 = ms.state[1] + b;
    uint32_t h2 = ms.state[2] + c;
    uint32_t h3 = ms.state[3] + d;
    uint32_t h4 = ms.state[4] + e;
    uint32_t h5 = ms.state[5] + f;
    uint32_t h6 = ms.state[6] + g;
    uint32_t h7 = ms.state[7] + h;

    // --- SHA256 pass 2: SHA256(h0..h7) ---
    // Keep this in the hot path. Calling esp_sha()/mbedTLS per nonce costs
    // more in driver/lock overhead than the one-block compression itself.
    W[0] = h0;
    W[1] = h1;
    W[2] = h2;
    W[3] = h3;
    W[4] = h4;
    W[5] = h5;
    W[6] = h6;
    W[7] = h7;
    W[8] = 0x80000000u;
    W[9] = 0; W[10] = 0; W[11] = 0; W[12] = 0; W[13] = 0; W[14] = 0;
    W[15] = 0x00000100u;
    for (int t = 16; t < 64; ++t) {
        R(W, t);
    }

    a = 0x6A09E667u;
    b = 0xBB67AE85u;
    c = 0x3C6EF372u;
    d = 0xA54FF53Au;
    e = 0x510E527Fu;
    f = 0x9B05688Cu;
    g = 0x1F83D9ABu;
    h = 0x5BE0CD19u;

    for (int t = 0; t < 64; ++t) {
        uint32_t t1 = h + S3(e) + F1(e, f, g) + K[t] + W[t];
        uint32_t t2 = S2(a) + F0(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    h0 = 0x6A09E667u + a;
    h1 = 0xBB67AE85u + b;
    h2 = 0x3C6EF372u + c;
    h3 = 0xA54FF53Au + d;
    h4 = 0x510E527Fu + e;
    h5 = 0x9B05688Cu + f;
    h6 = 0x1F83D9ABu + g;
    h7 = 0x5BE0CD19u + h;

    out[ 0]=h0>>24; out[ 1]=h0>>16; out[ 2]=h0>>8; out[ 3]=h0;
    out[ 4]=h1>>24; out[ 5]=h1>>16; out[ 6]=h1>>8; out[ 7]=h1;
    out[ 8]=h2>>24; out[ 9]=h2>>16; out[10]=h2>>8; out[11]=h2;
    out[12]=h3>>24; out[13]=h3>>16; out[14]=h3>>8; out[15]=h3;
    out[16]=h4>>24; out[17]=h4>>16; out[18]=h4>>8; out[19]=h4;
    out[20]=h5>>24; out[21]=h5>>16; out[22]=h5>>8; out[23]=h5;
    out[24]=h6>>24; out[25]=h6>>16; out[26]=h6>>8; out[27]=h6;
    out[28]=h7>>24; out[29]=h7>>16; out[30]=h7>>8; out[31]=h7;
    return true;
}

} // namespace WroomMiner
