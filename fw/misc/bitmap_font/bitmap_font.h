#pragma once
#include <stdint.h>

void bitmap_font_read_data(uint32_t glyph, uint8_t *buf);

static inline uint16_t rsh(uint16_t x, int8_t n)
{
  if (n < 0) return x << -n;
  else return x >> n;
}

// Blit a row of 12 bits onto a binary buffer
// Note: 200*200 is within 16 bits
static inline void blit_row(uint8_t *a, uint16_t bit_offs, uint16_t bitset)
{
  a += bit_offs / 8;
  bit_offs %= 8;
  a[0] |= rsh(bitset, 12 - 8 * 1 + bit_offs);
  a[1] |= rsh(bitset, 12 - 8 * 2 + bit_offs) & 0xff;
  a[2] |= rsh(bitset, 12 - 8 * 3 + bit_offs) & 0xff;
}

// Returns `advance_x`
static inline uint8_t bitmap_font_render_glyph(
  uint8_t *img, uint16_t w, uint16_t h, uint32_t glyph, uint16_t r0, uint16_t c0
) {
  uint8_t data[18];
  bitmap_font_read_data(glyph, data);

  int8_t bbx_y = (data[0] & 0xf);
  if (bbx_y >= 0x8) bbx_y -= 0x10;

  for (uint8_t r = 0; r < 12; r++) {
    int16_t r1 = -bbx_y + r0 + r;
    if (r1 < 0 || r1 >= h) continue;
    int16_t c1 = c0;

    uint16_t row_bitmap = (r % 2 == 0 ?
      ((uint16_t)data[1 + r / 2 * 3 + 0] << 4) | (data[1 + r / 2 * 3 + 1] >> 4) :
      ((uint16_t)(data[1 + r / 2 * 3 + 1] & 0x0f) << 8) | (data[1 + r / 2 * 3 + 2])
    );

    // Write the row to the bitmap
    // Caveat: does not handle horizontal out-of-bound conditions
    blit_row(img, r1 * w + c1, row_bitmap);
  }

  return data[0] >> 4;
}

// gcc -x c bitmap_font.h -O2 -DBITMAP_FONT_LOCAL_TEST
#ifdef BITMAP_FONT_LOCAL_TEST
#include <assert.h>
#include <stdio.h>

static FILE *f;

void bitmap_font_read_data(uint32_t glyph, uint8_t *buf)
{
  fseek(f, 0x0 + glyph * 2, SEEK_SET);
  uint8_t idxbuf[2];
  fread(idxbuf, 2, 1, f);

  uint16_t idx = ((uint16_t)idxbuf[0] << 8) | idxbuf[1];
  fseek(f, 0x20000 + idx * 19, SEEK_SET);
  fread(buf, 19, 1, f);
}

int main()
{
  f = fopen("wenquanyi_9pt.bin", "rb");
  assert(f != NULL);

  static uint8_t a[200][25] = {{ 0 }};
  uint16_t r = 3, c = 1;
  c += bitmap_font_render_glyph((uint8_t *)a, 200, 200, 0x21E1, r, c);
  c += bitmap_font_render_glyph((uint8_t *)a, 200, 200, '1', r, c);
  c += bitmap_font_render_glyph((uint8_t *)a, 200, 200, 'i', r, c);
  c += bitmap_font_render_glyph((uint8_t *)a, 200, 200, 'j', r, c);
  c += bitmap_font_render_glyph((uint8_t *)a, 200, 200, 'm', r, c);
  c += bitmap_font_render_glyph((uint8_t *)a, 200, 200, 0x591C, r, c);
  c += bitmap_font_render_glyph((uint8_t *)a, 200, 200, ')', r, c);
  c += bitmap_font_render_glyph((uint8_t *)a, 200, 200, '&', r, c);
  c += bitmap_font_render_glyph((uint8_t *)a, 200, 200, 0x22E0, r, c);
  c += bitmap_font_render_glyph((uint8_t *)a, 200, 200, 0x0257, r, c);
  c += bitmap_font_render_glyph((uint8_t *)a, 200, 200, 'l', r, c);
  c += bitmap_font_render_glyph((uint8_t *)a, 200, 200, 'l', r, c);

  for (int r = 0; r < 20; r++) {
    for (int c = 0; c < 10; c++)
      for (int b = 7; b >= 0; b--) {
        putchar((a[r][c] & (1 << b)) ? '#' : '.');
        putchar(' ');
      }
    putchar('\n');
  }

  fclose(f);
  return 0;
}
#endif
