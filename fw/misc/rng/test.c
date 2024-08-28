#include "twofish.h"
#include <stdint.h>
#include <stdio.h>

static int draw_card(const uint32_t *pool, const size_t len)
{
  uint32_t key[8] = { 0 };
  for (int i = 0; i < len; i++) key[i % 8] ^= pool[i];
  twofish_set_key(key, 256);

  int n_itrs = (len + 3) / 4;
  uint32_t block[2][4] = {{ 0 }};
  uint32_t accum = 0;
  // Twofish cipher in CBC mode
  for (int it = 0; it < n_itrs * 2; it++) {
    uint32_t *plain = block[it % 2];
    uint32_t *cipher = block[(it % 2) ^ 1];
    for (int i = 0; i < 4; i++) plain[i] ^= pool[(it * 4 + i) % len];
    twofish_encrypt(plain, cipher);
    for (int i = 0; i < 4; i++) accum ^= cipher[i];
    if (it > n_itrs && accum < 0x100000000 - 0x100000000 % 37 && accum % 37 != 0) break;
    if (it == n_itrs * 2 - 1) accum = 34;
  }

  uint8_t card_id = accum % 37 - 1;
  return card_id;
}

int main()
{
  uint32_t pool[50] = { 0 };
  int count[36] = { 0 };

  for (int i = 0; i < 2000 * 36; i++) {
    pool[0] = i;
    int c = draw_card(pool, sizeof pool / sizeof pool[0]);
    // printf("%3d %2d\n", i, c);
    count[c]++;
  }
  for (int i = 0; i < 36; i++) printf("%3d%c", count[i], i == 35 ? '\n' : ' ');

  return 0;
}
