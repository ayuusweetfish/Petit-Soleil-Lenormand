#include "xoodoo.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

// https://github.com/XKCP/XKCP/blob/ade40f8/lib/low/Xoodoo/ref/Xoodoo-reference.c
typedef uint32_t tXoodooLane;
#define NROWS       3
#define NCOLUMS     4
#define NLANES      (NCOLUMS*NROWS)
#define index(__x,__y) ((((__y) % NROWS) * NCOLUMS) + ((__x) % NCOLUMS))
#define ROTL32(_x, _n) (((_x) << (_n)) | ((_x) >> (32 - (_n))))
#define Dump(...)
static void Xoodoo_Round( tXoodooLane * a, tXoodooLane rc )
{
    unsigned int x, y;
    tXoodooLane    b[NLANES];
    tXoodooLane    p[NCOLUMS];
    tXoodooLane    e[NCOLUMS];

    /* Theta: Column Parity Mixer */
    for (x=0; x<NCOLUMS; ++x)
        p[x] = a[index(x,0)] ^ a[index(x,1)] ^ a[index(x,2)];
    for (x=0; x<NCOLUMS; ++x)
        e[x] = ROTL32(p[(x-1)%4], 5) ^ ROTL32(p[(x-1)%4], 14);
    for (x=0; x<NCOLUMS; ++x)
        for (y=0; y<NROWS; ++y)
            a[index(x,y)] ^= e[x];
    Dump("Theta", a, 2);

    /* Rho-west: plane shift */
    for (x=0; x<NCOLUMS; ++x) {
        b[index(x,0)] = a[index(x,0)];
        b[index(x,1)] = a[index(x-1,1)];
        b[index(x,2)] = ROTL32(a[index(x,2)], 11);
    }
    memcpy( a, b, sizeof(b) );
    Dump("Rho-west", a, 2);
        
    /* Iota: round constant */
    a[0] ^= rc;
    Dump("Iota", a, 2);

    /* Chi: non linear layer */
    for (x=0; x<NCOLUMS; ++x)
        for (y=0; y<NROWS; ++y)
            b[index(x,y)] = a[index(x,y)] ^ (~a[index(x,y+1)] & a[index(x,y+2)]);
    memcpy( a, b, sizeof(b) );
    Dump("Chi", a, 2);

    /* Rho-east: plane shift */
    for (x=0; x<NCOLUMS; ++x) {
        b[index(x,0)] = a[index(x,0)];
        b[index(x,1)] = ROTL32(a[index(x,1)], 1);
        b[index(x,2)] = ROTL32(a[index(x+2,2)], 8);
    }
    memcpy( a, b, sizeof(b) );
    Dump("Rho-east", a, 2);

}

void test(void (*round_fn)(uint32_t *, uint32_t))
{
  uint32_t pool[20] = {
    0x9875b068, 0xeea33f5f, 0xef85b724, 0xfe677cc9, 0x29991641, 0x9f5b0262, 0x10bf9972, 0x67c6d70e, 0x5a0c8364, 0x51058dab,
    0x3424e405, 0x3aa33b57, 0xb30ad3ab, 0x9176003d, 0xef5acd07, 0x42458f81, 0x906f6f08, 0x8d8c2c64, 0xe8368da5, 0x1ae9fe31,
  };

  round_fn(pool, 0x00000058);
  round_fn(pool + 8, 0x00000058);

  for (int i = 0; i < 20; i++) printf("%08x%c", pool[i], i % 10 == 9 ? '\n' : ' ');
}

void xoodoo12(uint32_t s[12])
{
  const uint32_t rc[12] = {
    0x0058, 0x0038, 0x03c0, 0x00d0, 0x0120, 0x0014,
    0x0060, 0x002c, 0x0380, 0x00f0, 0x01a0, 0x0012,
  };
  for (int i = 0; i < 12; i++) xoodoo(s, rc[i]);
}

// https://keccak.team/xoodyak.html
// https://tosc.iacr.org/index.php/ToSC/article/view/8618/8184, Algorithm 2
void xoodyak_hash(const uint8_t *msg, size_t msg_len, uint8_t *out)
{
  uint8_t s[48] __attribute__((aligned(4))) = {0};

  size_t p = 0;
  // AbsorbAny: Down(0x01), {Up(0x00), Down(0x00)}*
  do {
    size_t n = (msg_len - p) > 16 ? 16 : (msg_len - p);
    for (int i = 0; i < n; i++) s[i] ^= msg[p + i];
    s[n] ^= 0x01;
    if (p == 0) s[47] ^= 0x01;  // Down(0x01)
    xoodoo12((void *)s);
    p += n;
  } while (p < msg_len);

  // SqueezeAny: {Down(0x00)}*
  for (size_t p = 0; p < 32; p += 16) {
    memcpy(out + p, s, 16);
    s[0] ^= 0x01;
    xoodoo12((void *)s);
  }
}

int main()
{
  printf("====== Against reference: outputs should be identical ======\n\n");
  test(Xoodoo_Round);
  printf("\n");
  test(xoodoo);
  printf("\n");

  printf("====== Test vectors ======\n\n");

  // (1) Xoodoo-12 single-round
  uint32_t s[12] = {0};
  xoodoo12(s);
  for (int i = 0; i < 12; i++) printf("%08x%c", s[i], i % 6 == 5 ? '\n' : ' ');
  // 89d5d88d a963fcbf 1b232d19 ffa5a014 36b18106 afc7c1fe
  // aee57cbe a77540bd 2e86e870 fef5b7c9 8b4fadf2 5e4f4062
  /*
    Obtained by matching against `Xoodoo_Round()` and other implementations
    The same results are present at:
    - https://github.com/jedisct1/charm/blob/a726cd1/verify/xoodoo.cry#L411
    - https://github.com/usubalang/usuba/blob/4553c25c/checks/correctness/xoodoo/main.c#L90
    - https://github.com/deatil/go-cryptobin/blob/965cda0/cipher/xoodoo/xoodoo/example_test.go#L15
    - https://github.com/inmcm/xoodoo/blob/94516a1/xoodoo/xoodoo_test.go#L26
  */
  printf("\n");

  // (2) Test vector in Xoodyak implementation
  // https://csrc.nist.gov/CSRC/media/Projects/lightweight-cryptography/documents/finalist-round/updated-submissions/xoodyak.zip: Implementations/crypto_hash/xoodyakround3/ref/LWC_HASH_KAT_256.txt
  struct { int msg_len; const uint8_t msg[48]; } cases[] = {
    {0, {}},
    {1, {0x00}},
    {2, {0x00, 0x01}},
    {3, {0x00, 0x01, 0x02}},
    {4, {0x00, 0x01, 0x02, 0x03}},
    {5, {0x00, 0x01, 0x02, 0x03, 0x04}},
    {6, {0x00, 0x01, 0x02, 0x03, 0x04, 0x05}},
    {7, {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06}},
    {8, {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07}},
    {14, {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d}},
    {15, {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e}},
    {16, {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f}},
    {17, {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10}},
    {31, {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e}},
    {32, {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f}},
    {33, {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20}},
  };
  for (int i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    uint8_t digest[32];
    xoodyak_hash(cases[i].msg, cases[i].msg_len, digest);
    for (int i = 0; i < 32; i++) printf("%02x", (unsigned)digest[i]);
    putchar('\n');
  }
  /*
    ea152f2b47bce24efb66c479d4adf17bd324d806e85ff75ee369ee50dc8f8bd1
    27921f8ddf392894460b70b3ed6c091e6421b7d2147dcd6031d7efebad3030cc
    dd3f12e89db41c61d3c05779705fa946a8c69c79eefdc1b4a966a5f1ab35073d
    72abd350dc287e8c4b95dd37bd796d79f90026c1bd4e0d99d2117baab26bc2ca
    a13ae46f62e433ce4cad9e4f24c46f37b6b3815c8539a3659daaecaae1ab8fdb
    042383068c131a0d365b781dfcb20e855f4a68de2072aa8d1e16181563d6f622
    415d3a751952454c1bb900700a2eb8c2814f0a30c34bc25cc37d3de96159f4ae
    072f0834cc8fe7996e90aded60228c18791e3a3da38a3831da880edf7869909c
    c826d28c7f5bf948fba9bb5ea028b4e377f1de86ec5a2a1511ba4d692968efd5
    d0fa0c36d76f9335615ce15e4a8b78c71b31f03dea5eab786ca91a887da85de4
    db4c9cfe9d385d8ca329e27aeb495a0816c1ab051a57c231a134082661d71bed
    9ea695347cdddff9bc63ece30fe231441d581768fe223dd6bd7367094fd216b3
    20593b39bb6d595019331601244411323f713085bb1a30218c972b96d9b7b7b3
    b91e0c762169748d4e2b8d4972b63a4866caad1b5ebfb7f37deadeb4424df768
    cebe4aff9eac2218017dda5f8207ba830e989187256539bd7d31ae5e94ff0c6e
    249cfccd50d66e722e80e79002ce3b302b4ca067483ab9cdeb474dbf555b7633
  */

  return 0;
}
