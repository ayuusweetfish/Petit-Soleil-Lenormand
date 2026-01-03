// gcc -std=c99 all.c -O2 -lm
// ./a.out > cards.bin

/* card_commentary.txt:
for i, n in enumerate(['Rider', 'Clover', 'Ship', 'House', 'Tree', 'Clouds', 'Snake', 'Coffin', 'Bouquet', 'Scythe', 'Whip', 'Birds', 'Child', 'Fox', 'Bear', 'Stars', 'Stork', 'Dog', 'Tower', 'Garden', 'Mountain', 'Crossroads', 'Mice', 'Heart', 'Ring', 'Book', 'Letter', 'Animus', 'Anima', 'Lily', 'Sun', 'Moon', 'Key', 'Fish', 'Anchor', 'Cross']):
  print('====== %d ======\nlorem ipsum\n文字 %s\n' % (i + 1, n))
*/

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"  // stb_image - v2.30 (013ac3b)

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Format:
// (+    0) 200*200 card illustration image
// (+ 5000) 200*200 card illustration image (shadow)
// (+10000) 1 card name side/direction
// (+10001) 200*40 card name image (colour)
// (+11001) 200*40 card name image (mask)
// (+12001) text
// Total size 13001

static uint8_t *read_file(const char *restrict path, size_t *restrict len)
{
  FILE *f = fopen(path, "r");
  if (f == NULL) {
    fprintf(stderr, "Cannot open file %s\n", path);
    exit(1);
  }
  fseek(f, 0, SEEK_END);
  size_t size = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *p = malloc(size);
  fread(p, size, 1, f);
  fclose(f);

  *len = size;
  return p;
}

static uint8_t *read_image(const char *restrict path, int expected_w, int expected_h)
{
  int w, h;
  uint8_t *p = stbi_load(path, &w, &h, NULL, 4);
  if (p == NULL) {
    fprintf(stderr, "Cannot open image %s\n", path);
    exit(1);
  }
  if (w != expected_w || h != expected_h) {
    fprintf(stderr, "Image %s size incorrect: expected %dx%d, got %dx%d\n",
      path, expected_w, expected_h, w, h);
    exit(1);
  }
  return p;
}

static uint32_t utf8_codepoint(const uint8_t *restrict a, size_t *restrict _p, size_t n)
{
  size_t p = *_p;
  uint32_t c;

  uint8_t b1 = a[p++];
  if ((b1 & 0x80) == 0x00) {
    c = (b1 & 0x7f);
    goto fin;
  }

  if (p >= n) return 0xffffffff;
  uint8_t b2 = a[p++];
  if ((b1 & 0xe0) == 0xc0) {
    c = ((uint32_t)(b1 & 0x1f) <<  6) |
        (b2 & 0x3f);
    goto fin;
  }

  if (p >= n) return 0xffffffff;
  uint8_t b3 = a[p++];
  if ((b1 & 0xf0) == 0xe0) {
    c = ((uint32_t)(b1 & 0x0f) << 12) |
        ((uint32_t)(b2 & 0x3f) <<  6) |
        (b3 & 0x3f);
    goto fin;
  }

  if (p >= n) return 0xffffffff;
  uint8_t b4 = a[p++];
  if ((b1 & 0xf8) == 0xf0) {
    c = ((uint32_t)(b1 & 0x0f) << 18) |
        ((uint32_t)(b2 & 0x3f) << 12) |
        ((uint32_t)(b3 & 0x3f) <<  6) |
        (b4 & 0x3f);
    goto fin;
  }

  // Invalid sequence
  return 0xffffffff;

fin:
  *_p = p;
  return c;
}

int main()
{
  size_t directions_len;
  uint8_t *directions = read_file("card_directions.txt", &directions_len);
  if (directions_len < 36) {
    fprintf(stderr, "Insufficient directions (less than 36)\n");
    exit(1);
  }

  size_t text_len;
  uint8_t *text = read_file("card_commentary.txt", &text_len);

  // Find starts
  const uint8_t *cmt_start[36] = { NULL };
  int cmt_len[36] = { 0 };
  for (int i = 0; i < 36; i++) {
    char delim[64];
    int len = snprintf(delim, sizeof delim, "====== %d ======\n", i + 1);
    const uint8_t *ptr = (const uint8_t *)strstr((const char *)(i == 0 ? text : cmt_start[i - 1]), delim);
    if (ptr == NULL) {
      fprintf(stderr, "Cannot find start of commentary for card %d\n", i + 1);
      exit(1);
    }
    if (i > 0) cmt_len[i - 1] = ptr - cmt_start[i - 1];
    cmt_start[i] = ptr + len;
  }
  cmt_len[35] = text + text_len - cmt_start[35];
  for (int i = 0; i < 36; i++) {
    // Trim trailing whitespace
    while (cmt_len[i] > 0 && isspace(cmt_start[i][cmt_len[i] - 1]))
      cmt_len[i]--;
  }

  for (int i = 0; i < 36; i++) {
    char path[64];
    fprintf(stderr, "Card %d\n", i + 1);

    snprintf(path, sizeof path, "card_illustrations/%02d_outline.png", i + 1);
    fprintf(stderr, "  illust (%s)\n", path);
    uint8_t *p_illust = read_image(path, 200, 200);
    for (int i = 0, byte = 0; i < 200 * 200; i++) {
      byte = (byte << 1) | (p_illust[i * 4] >= 160);
      if (i % 8 == 7) { putchar(byte); byte = 0; }
    }
    stbi_image_free(p_illust);

    snprintf(path, sizeof path, "card_illustrations/%02d_shadow.png", i + 1);
    fprintf(stderr, "  shadow (%s)\n", path);
    uint8_t *p_shadow = read_image(path, 200, 200);
    for (int i = 0, byte = 0; i < 200 * 200; i++) {
      byte = (byte << 1) | (p_shadow[i * 4] >= 160);
      if (i % 8 == 7) { putchar(byte); byte = 0; }
    }
    stbi_image_free(p_shadow);

    // Card name direction
    putchar(directions[i] - '0');

    snprintf(path, sizeof path, "card_names/%d.png", i + 1);
    fprintf(stderr, "  title (%s)\n", path);
    uint8_t *p_name = read_image(path, 200, 40);
    for (int i = 0, byte = 0; i < 200 * 40; i++) {
      byte = (byte << 1) | (p_name[i * 4] < 160);
      if (i % 8 == 7) { putchar(byte); byte = 0; }
    }
    for (int i = 0, byte = 0; i < 200 * 40; i++) {
      byte = (byte << 1) | (p_name[i * 4 + 3] >= 160);
      if (i % 8 == 7) { putchar(byte); byte = 0; }
    }
    stbi_image_free(p_name);

    fprintf(stderr, "  text (length %d)\n", cmt_len[i]);
    int char_count = 0;
    for (size_t j = 0; j < cmt_len[i]; ) {
      uint32_t c = utf8_codepoint(cmt_start[i], &j, cmt_len[i]);
      if (c > 0xffff) {
        fprintf(stderr, "Invalid code point U+%x (only 16-bit supported)\n", c);
        exit(1);
      }
      putchar(c >> 8);
      putchar(c & 0xff);
      char_count++;
    }
    for (int j = char_count * 2; j < 1000; j++) putchar(0);
  }

  return 0;
}
