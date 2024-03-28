// ====== keccak-tiny.c =======

/** libkeccak-tiny
 *
 * A single-file implementation of SHA-3 and SHAKE.
 *
 * Implementor: David Leon Gil
 * License: CC0, attribution kindly requested. Blame taken too,
 * but not liability.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/******** The Keccak-f[1600] permutation ********/

/*** Constants. ***/
static const uint8_t rho[24] = \
  { 1,  3,   6, 10, 15, 21,
    28, 36, 45, 55,  2, 14,
    27, 41, 56,  8, 25, 43,
    62, 18, 39, 61, 20, 44};
static const uint8_t pi[24] = \
  {10,  7, 11, 17, 18, 3,
    5, 16,  8, 21, 24, 4,
   15, 23, 19, 13, 12, 2,
   20, 14, 22,  9, 6,  1};
static const uint64_t RC[24] = \
  {1ULL, 0x8082ULL, 0x800000000000808aULL, 0x8000000080008000ULL,
   0x808bULL, 0x80000001ULL, 0x8000000080008081ULL, 0x8000000000008009ULL,
   0x8aULL, 0x88ULL, 0x80008009ULL, 0x8000000aULL,
   0x8000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL, 0x8000000000008003ULL,
   0x8000000000008002ULL, 0x8000000000000080ULL, 0x800aULL, 0x800000008000000aULL,
   0x8000000080008081ULL, 0x8000000000008080ULL, 0x80000001ULL, 0x8000000080008008ULL};

/*** Helper macros to unroll the permutation. ***/
#define rol(x, s) (((x) << s) | ((x) >> (64 - s)))
#define REPEAT6(e) e e e e e e
#define REPEAT24(e) REPEAT6(e e e e)
#define REPEAT5(e) e e e e e
#define FOR5(v, s, e) \
  v = 0;            \
  REPEAT5(e; v += s;)

/*** Keccak-f[1600] ***/
static inline void keccak_f(void* state) {
  uint64_t* a = (uint64_t*)state;
  uint64_t b[5] = {0};
  uint64_t t = 0;
  uint8_t x, y;

  for (int i = 0; i < 24; i++) {
    // Theta
    FOR5(x, 1,
         b[x] = 0;
         FOR5(y, 5,
              b[x] ^= a[x + y]; ))
    FOR5(x, 1,
         FOR5(y, 5,
              a[y + x] ^= b[(x + 4) % 5] ^ rol(b[(x + 1) % 5], 1); ))
    // Rho and pi
    t = a[1];
    x = 0;
    REPEAT24(b[0] = a[pi[x]];
             a[pi[x]] = rol(t, rho[x]);
             t = b[0];
             x++; )
    // Chi
    FOR5(y,
       5,
       FOR5(x, 1,
            b[x] = a[y + x];)
       FOR5(x, 1,
            a[y + x] = b[x] ^ ((~b[(x + 1) % 5]) & b[(x + 2) % 5]); ))
    // Iota
    a[0] ^= RC[i];
  }
}

// ====== End of keccak-tiny.c ======

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static uint8_t state[200] = { 0 };

static inline void feed(
  const uint8_t *buffer, size_t in_len,
  uint8_t *out, size_t out_len, size_t *out_pos
) {
  assert(out_len % 64 == 0);
  uint_fast8_t state_pos = 0;

  #define yield_squeeze do {                \
    keccak_f(state);                        \
    if (out) {                              \
      for (uint_fast8_t i = 0; i < 64; i++) \
        out[*out_pos + i] ^= state[i];      \
      *out_pos = (*out_pos + 64) % out_len; \
    }                                       \
  } while (0)

  for (size_t i = 0; i < in_len; i++) {
    state[state_pos++] ^= buffer[i];
    if (state_pos == 64) {
      yield_squeeze;
      state_pos = 0;
    }
  }
  // Pad 10*1
  state[state_pos] ^= 0x80;
  state[63] ^= 0x01;
  // Yield last block
  yield_squeeze;

  #undef yield_squeeze
}

static inline void whiten(uint8_t *out, size_t out_len)
{
  assert(out_len % 64 == 0);
  for (size_t p = 0; p < out_len; p += 64) {
    keccak_f(state);
    for (uint_fast8_t i = 0; i < 64; i++)
      out[p + i] ^= state[i];
  }
}

int main(int argc, char *argv[])
{
/*
  uint8_t s[200] = { 0 };
  keccak_f(s);
  for (int i = 0; i < 200; i++)
    printf("%02x%s", s[i], i % 40 == 39 ? "\n" : i % 8 == 7 ? " - " : " ");
*/

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
