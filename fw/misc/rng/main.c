#include <stdio.h>
#include <stdint.h>

uint32_t *set_key(const uint32_t in_key[], const uint32_t key_len);
void encrypt(const uint32_t in_blk[4], uint32_t out_blk[4]);
void decrypt(const uint32_t in_blk[4], uint32_t out_blk[4]);

int main()
{
  uint8_t key[16] = {2, 0, 2, 4, 0, 3, 1, 8, 11, 22, 33, 44, 55, 66, 77, 88};
  set_key((uint32_t *)key, 128);

  uint8_t plain[16] = {
    0xfe, 0xed, 0xba, 0xcc, 0x5f, 0x37, 0x59, 0xdf,
    0x24, 0x3f, 0x6a, 0x88, 0x85, 0xa3, 0x08, 0xd3,
  };
  uint8_t cipher[16] = { 0 };
  encrypt((uint32_t *)plain, (uint32_t *)cipher);
  for (int i = 0; i < 16; i++)
    printf("%02x%c", cipher[i], i == 15 ? '\n' : ' ');
  // 62 fb 3c b8 a4 ba 2f 98 0e df f2 67 c7 2a db a3

  for (int i = 0; i < 16; i++) plain[i] = 0;
  decrypt((uint32_t *)cipher, (uint32_t *)plain);
  for (int i = 0; i < 16; i++)
    printf("%02x%c", plain[i], i == 15 ? '\n' : ' ');

  puts("Done");
  return 0;
}
