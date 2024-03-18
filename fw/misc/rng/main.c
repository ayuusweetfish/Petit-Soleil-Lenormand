#include <stdio.h>
#include <stdint.h>
#include "twofish.h"

int main()
{
  Twofish_initialise();

  Twofish_key k;
  uint8_t key[8] = {2, 0, 2, 4, 0, 3, 1, 8};
  Twofish_prepare_key(key, 8, &k);

  uint8_t plain[16] = {
    0xfe, 0xed, 0xba, 0xcc, 0x5f, 0x37, 0x59, 0xdf,
    0x24, 0x3f, 0x6a, 0x88, 0x85, 0xa3, 0x08, 0xd3,
  };
  uint8_t cipher[16] = { 0 };
  Twofish_encrypt(&k, plain, cipher);
  for (int i = 0; i < 16; i++)
    printf("%02x%c", cipher[i], i == 15 ? '\n' : ' ');

  for (int i = 0; i < 16; i++) plain[i] = 0;
  Twofish_decrypt(&k, cipher, plain);
  for (int i = 0; i < 16; i++)
    printf("%02x%c", plain[i], i == 15 ? '\n' : ' ');

  puts("Done");
  return 0;
}
