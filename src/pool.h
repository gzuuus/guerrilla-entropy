#pragma once
#include <stdint.h>
#include <string.h>
#include <mbedtls/sha256.h>

// Minimal hash-based entropy pool (sponge-like, SHA-256).
// Absorb raw source bytes into a 32-byte state; squeeze uniform output.
// HW-accelerated on ESP32-S3 via mbedtls -> the SHA peripheral.
//
// Invariant: output is SHA-256 derived and thus uniform, so it stays
// XOR-mixable with external entropy (see AGENTS.md THE invariant).
class Pool {
 public:
  static constexpr size_t BLOCK = 32;  // SHA-256 digest size

  void begin() {
    memset(state_, 0, sizeof(state_));
    counter_ = 0;
  }

  // Absorb raw bytes into the state (reseed).
  void add(const uint8_t* in, size_t n) {
    sha(state_, 32, in, n, state_);
  }

  // Squeeze n uniform bytes out.
  void extract(uint8_t* out, size_t n) {
    size_t i = 0;
    while (i < n) {
      uint8_t blk[BLOCK];
      uint64_t c = ++counter_;                  // domain-separate each block
      uint8_t ctr[8];
      for (int b = 0; b < 8; b++) ctr[b] = (uint8_t)(c >> (8 * b));
      sha(state_, 32, ctr, sizeof(ctr), blk);
      size_t take = (n - i < BLOCK) ? (n - i) : BLOCK;
      memcpy(out + i, blk, take);
      i += take;
    }
  }

 private:
  static void sha(const uint8_t* a, size_t alen,
                  const uint8_t* b, size_t blen,
                  uint8_t out[BLOCK]) {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);              // 0 = SHA-256 (not 224)
    mbedtls_sha256_update(&ctx, a, alen);
    mbedtls_sha256_update(&ctx, b, blen);
    mbedtls_sha256_finish(&ctx, out);
    mbedtls_sha256_free(&ctx);
  }

  uint8_t state_[32];
  uint64_t counter_;
};
