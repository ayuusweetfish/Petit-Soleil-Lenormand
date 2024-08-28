#include "twofish.h"
#include <stdint.h>
#include <stdio.h>

int draw_card(const uint8_t *pool, const size_t len)
{
  uint8_t key[32] = { 0 };
  for (int i = 0; i < len; i++) key[i % 32] ^= pool[i];
  twofish_set_key((uint32_t *)key, 256);

  int n_itrs = (len + 15) / 16;
  uint8_t block[2][16] = {{ 0 }};
  uint32_t accum = 0;
  // Twofish cipher in CBC mode
  for (int it = 0; it < n_itrs * 2; it++) {
    uint8_t *plain = block[it % 2];
    uint8_t *cipher = block[(it % 2) ^ 1];
    for (int i = 0; i < 16; i++) plain[i] ^= pool[(it * 16 + i) % len];
    twofish_encrypt((uint32_t *)block, (uint32_t *)cipher);
    for (int i = 0; i < 4; i++) accum ^= ((uint32_t *)cipher)[i];
    if (it > n_itrs && accum < 0x100000000 - 0x100000000 % 37 && accum % 37 != 0) break;
    if (it == n_itrs * 2 - 1) accum = 34;
  }

  uint8_t card_id = accum % 37 - 1;
  return card_id;
}

int main()
{
  uint8_t pool[200] = { 0 };
  int count[36] = { 0 };

  for (int i = 0; i < 2000 * 36; i++) {
    pool[2] = i >> 16;
    pool[1] = i >>  8;
    pool[0] = i >>  0;
    int c = draw_card(pool, sizeof pool);
    // printf("%3d %2d\n", i, c);
    count[c]++;
  }
  for (int i = 0; i < 36; i++) printf("%3d%c", count[i], i == 35 ? '\n' : ' ');

  return 0;
}
