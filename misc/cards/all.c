// gcc -O2 -DSTB_IMAGE_IMPLEMENTATION -c -x c stb_image.h
// gcc -std=c99 all.c stb_image.o

#include "stb_image.h"  // stb_image - v2.30 (013ac3b)

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Format:
// (+    0) 200*200 card illustration image
// (+ 5000) 200*40 card name image
// (+ 6000) text
// Total size 7000

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

int main()
{
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

    snprintf(path, sizeof path, "card_illustrations/%d.png", i + 1);
    fprintf(stderr, "  illust (%s)\n", path);
    uint8_t *p_illust = read_image(path, 200, 200);

    snprintf(path, sizeof path, "card_names/%d.png", i + 1);
    fprintf(stderr, "  title (%s)\n", path);
    uint8_t *p_name = read_image(path, 200, 40);

    stbi_image_free(p_illust);
    stbi_image_free(p_name);

    fprintf(stderr, "  text (length %d)\n", cmt_len[i]);
  }

  return 0;
}
