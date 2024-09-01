#pragma once

// https://github.com/XKCP/XKCP/blob/ade40f8/lib/low/Xoodoo/plain/Xoodoo-optimized.c

#define ROTL32(_x, _n) (((_x) << (_n)) | ((_x) >> (32 - (_n))))

#pragma GCC push_options
#pragma GCC optimize("O3")
static inline void xoodoo(uint32_t *a, uint32_t rc)
{
  uint32_t
    a00 = a[0+0], a01 = a[0+1], a02 = a[0+2], a03 = a[0+3],
    a10 = a[4+0], a11 = a[4+1], a12 = a[4+2], a13 = a[4+3],
    a20 = a[8+0], a21 = a[8+1], a22 = a[8+2], a23 = a[8+3];
  uint32_t v1, v2;

  // Theta: Column Parity Mixer
  v1 = a03 ^ a13 ^ a23;
  v2 = a00 ^ a10 ^ a20;
  v1 = ROTL32(v1, 5) ^ ROTL32(v1, 14);
  a00 ^= v1;
  a10 ^= v1;
  a20 ^= v1;
  v1 = a01 ^ a11 ^ a21;
  v2 = ROTL32(v2, 5) ^ ROTL32(v2, 14);
  a01 ^= v2;
  a11 ^= v2;
  a21 ^= v2;
  v2 = a02 ^ a12 ^ a22;
  v1 = ROTL32(v1, 5) ^ ROTL32(v1, 14);
  a02 ^= v1;
  a12 ^= v1;
  a22 ^= v1;
  v2 = ROTL32(v2, 5) ^ ROTL32(v2, 14);
  a03 ^= v2;
  a13 ^= v2;
  a23 ^= v2;

  // Rho-west: Plane shift
  a20 = ROTL32(a20, 11);
  a21 = ROTL32(a21, 11);
  a22 = ROTL32(a22, 11);
  a23 = ROTL32(a23, 11);
  v1 = a13;
  a13 = a12;
  a12 = a11;
  a11 = a10;
  a10 = v1;

  // Iota: Round constants
  a00 ^= rc;

  // Chi: Non linear step, on columns
  a00 ^= ~a10 & a20;
  a10 ^= ~a20 & a00;
  a20 ^= ~a00 & a10;

  a01 ^= ~a11 & a21;
  a11 ^= ~a21 & a01;
  a21 ^= ~a01 & a11;

  a02 ^= ~a12 & a22;
  a12 ^= ~a22 & a02;
  a22 ^= ~a02 & a12;

  a03 ^= ~a13 & a23;
  a13 ^= ~a23 & a03;
  a23 ^= ~a03 & a13;

  // Rho-east: Plane shift
  a10 = ROTL32(a10, 1);
  a11 = ROTL32(a11, 1);
  a12 = ROTL32(a12, 1);
  a13 = ROTL32(a13, 1);
  v1  = ROTL32(a23, 8);
  a23 = ROTL32(a21, 8);
  a21 = v1;
  v1  = ROTL32(a22, 8);
  a22 = ROTL32(a20, 8);
  a20 = v1;

  a[0+0] = a00, a[0+1] = a01, a[0+2] = a02, a[0+3] = a03,
  a[4+0] = a10, a[4+1] = a11, a[4+2] = a12, a[4+3] = a13,
  a[8+0] = a20, a[8+1] = a21, a[8+2] = a22, a[8+3] = a23;
}
#pragma GCC pop_options

#undef ROTL32
