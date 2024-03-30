// SPDX-License-Identifier: CC0-1.0

// A 4096-bit hash for multiple files,
// where each bit in each file affects all output bits equally
// and the image size is close to 2^4096
// Ayu, 2024 (vernal equinox)

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint8_t big_endian = 0;

// ====== The Keccak permutation ======
// Based on libkeccak-tiny by David Leon Gil under the CC0 permit

#define rol(x, s) (((x) << (s)) | ((x) >> (64 - (s))))

// Keccak-f[1600]
static inline void keccak_f(void *state) {
  static const uint8_t rho[24] = \
    { 1,  3,  6, 10, 15, 21,
     28, 36, 45, 55,  2, 14,
     27, 41, 56,  8, 25, 43,
     62, 18, 39, 61, 20, 44};
  static const uint8_t pi[24] = \
    {10,  7, 11, 17, 18,  3,
      5, 16,  8, 21, 24,  4,
     15, 23, 19, 13, 12,  2,
     20, 14, 22,  9,  6,  1};
  static const uint64_t RC[24] = \
    {0x0000000000000001ull, 0x0000000000008082ull, 0x800000000000808Aull,
     0x8000000080008000ull, 0x000000000000808Bull, 0x0000000080000001ull,
     0x8000000080008081ull, 0x8000000000008009ull, 0x000000000000008Aull,
     0x0000000000000088ull, 0x0000000080008009ull, 0x000000008000000Aull,
     0x000000008000808Bull, 0x800000000000008Bull, 0x8000000000008089ull,
     0x8000000000008003ull, 0x8000000000008002ull, 0x8000000000000080ull,
     0x000000000000800Aull, 0x800000008000000Aull, 0x8000000080008081ull,
     0x8000000000008080ull, 0x0000000080000001ull, 0x8000000080008008ull};

  uint64_t *a = (uint64_t *)state;
  uint64_t b[5] = {0};

  if (big_endian)
    for (int x = 0; x < 25; x++) a[x] = __builtin_bswap64(a[x]);

  for (int i = 0; i < 24; i++) {
    // Theta
    for (int x = 0; x < 5; x++) {
      b[x] = 0;
      for (int y = 0; y < 25; y += 5)
        b[x] ^= a[x + y];
    }
    for (int x = 0; x < 5; x++) {
      for (int y = 0; y < 25; y += 5)
        a[y + x] ^= b[(x + 4) % 5] ^ rol(b[(x + 1) % 5], 1);
    }
    // Rho and pi
    uint64_t t = a[1];
    for (int x = 0; x < 24; x++) {
      b[0] = a[pi[x]];
      a[pi[x]] = rol(t, rho[x]);
      t = b[0];
    }
    // Chi
    for (int y = 0; y < 25; y += 5) {
      for (int x = 0; x < 5; x++)
        b[x] = a[y + x];
      for (int x = 0; x < 5; x++)
        a[y + x] = b[x] ^ ((~b[(x + 1) % 5]) & b[(x + 2) % 5]);
    }
    // Iota
    a[0] ^= RC[i];
  }

  if (big_endian)
    for (int x = 0; x < 25; x++) a[x] = __builtin_bswap64(a[x]);
}

// ====== Main ======

// A Keccak-based PRNG
// Sponge construction, 10*1 padding, rate = 1024, capacity = 576

static uint8_t state[200] = { 0 };

// Feed a large block of data,
// squeezing into an output buffer (by xor'ing into it) along the way
static inline void feed(
  const uint8_t *buffer, size_t in_len,
  uint8_t *out, size_t out_len, size_t *out_pos
) {
  assert(out_len % 128 == 0);
  uint_fast8_t state_pos = 0;

  #define yield_squeeze do {                 \
    keccak_f(state);                         \
    if (out) {                               \
      for (uint_fast8_t i = 0; i < 128; i++) \
        out[*out_pos + i] ^= state[i];       \
      *out_pos = (*out_pos + 64) % out_len;  \
    }                                        \
  } while (0)

  for (size_t i = 0; i < in_len; i++) {
    state[state_pos++] ^= buffer[i];
    if (state_pos == 128) {
      yield_squeeze;
      state_pos = 0;
    }
  }
  // Pad 10*1
  state[state_pos] ^= 0x80;
  state[127] ^= 0x01;
  // Yield last block
  yield_squeeze;

  #undef yield_squeeze
}

// Squeeze values into an output buffer (by xor'ing into it)
static inline void whiten(uint8_t *out, size_t out_len)
{
  assert(out_len % 128 == 0);
  for (size_t p = 0; p < out_len; p += 128) {
    keccak_f(state);
    for (uint_fast8_t i = 0; i < 128; i++)
      out[p + i] ^= state[i];
  }
}

int main(int argc, char *argv[])
{
  // Check endianness
  uint16_t test = 0x1234;
  if (*(uint8_t *)&test != 0x34) big_endian = 1;

  if (argc <= 1) {
    fprintf(stderr, "usage: %s <file> ...\n", argv[0]);
    return 0;
  }

  // ====== Read files ======

  int n_files = argc - 1;
  struct input_file {
    uint8_t *content;
    size_t size;
  };
  struct input_file *f = malloc(sizeof(struct input_file) * n_files);

  for (int i = 0; i < n_files; i++) {
    const char *path = argv[i + 1];
    fprintf(stderr, "Reading %s...", path); fflush(stderr);

    FILE *f_in = fopen(argv[i + 1], "rb");
    if (f_in == NULL) {
      fprintf(stderr, "Cannot open %s\n", path);
      return 1;
    }

    fseek(f_in, 0, SEEK_END);
    long p = ftell(f_in);
    f[i].size = p;
    fseek(f_in, 0, SEEK_SET);
    // Check for overflow; this also covers the case where p < 0
    if (f[i].size != p) {
      fprintf(stderr, "Errors happened when seeking %s\n", path);
      return 1;
    }

    f[i].content = malloc(f[i].size);
    fread(f[i].content, f[i].size, 1, f_in);
    if (ferror(f_in) != 0) {
      fprintf(stderr, "Errors happened when reading %s\n", path);
      return 1;
    }

    fclose(f_in);
    fprintf(stderr, " (%ld B)\n", f[i].size); fflush(stderr);
  }

  // ====== Calculate the hash ======

  static uint8_t result[4096] = { 0 };

  // PRNG initialisation
  for (int i = 0; i < n_files; i++) {
    feed(f[i].content, f[i].size, NULL, 0, NULL);
  }

  // Initial whitening
  whiten(result, 4096);

  // Alternating absorption and squeeze
  size_t squeeze_pos = 0;
  for (int i = 0; i < n_files; i++)
    feed(f[i].content, f[i].size, result, 4096, &squeeze_pos);

  // Final whitening
  whiten(result, 4096);

  for (int i = 0; i < 4096; i++)
    printf("%02x", result[i]);
  putchar('\n');

  return 0;
}
