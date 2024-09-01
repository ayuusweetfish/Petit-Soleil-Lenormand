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

int main()
{
  static uint32_t pool[20] = {
    0x9875b068, 0xeea33f5f, 0xef85b724, 0xfe677cc9, 0x29991641, 0x9f5b0262, 0x10bf9972, 0x67c6d70e, 0x5a0c8364, 0x51058dab,
    0x3424e405, 0x3aa33b57, 0xb30ad3ab, 0x9176003d, 0xef5acd07, 0x42458f81, 0x906f6f08, 0x8d8c2c64, 0xe8368da5, 0x1ae9fe31,
  };

  Xoodoo_Round(pool, 0x00000058);
  Xoodoo_Round(pool + 8, 0x00000058);

  for (int i = 0; i < 20; i++) printf("%08x%c", pool[i], i % 10 == 9 ? '\n' : ' ');

  return 0;
}
