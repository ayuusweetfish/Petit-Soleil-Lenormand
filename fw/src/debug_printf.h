#ifndef RELEASE

#include <stdarg.h>
#include <stdio.h>

static uint8_t debug_buf[128];
static size_t debug_buf_ptr = 0;

__attribute__ ((noinline, used))
void debug_trap_line()
{
  *(volatile char *)debug_buf;
}

static inline void debug_putchar(uint8_t c)
{
  if (c == '\n') {
    debug_buf[debug_buf_ptr >= sizeof debug_buf ?
      (sizeof debug_buf - 1) : debug_buf_ptr] = '\0';
    debug_trap_line();
    debug_buf_ptr = 0;
  } else if (debug_buf_ptr <= sizeof debug_buf - 1) {
    debug_buf[debug_buf_ptr++] = c;
  }
}

__attribute__ ((format(printf, 1, 2)))
static void debug_printf(const char *restrict fmt, ...)
{
  static char s[32];
  va_list args;
  va_start(args, fmt);
  int r = vsnprintf(s, sizeof s, fmt, args);
  for (int i = 0; i < r && i < sizeof s - 1; i++) debug_putchar(s[i]);
  if (r >= sizeof s) {
    for (int i = 0; i < 3; i++) debug_putchar('.');
    debug_putchar('\n');
  }
}

#define printf(...) debug_printf(__VA_ARGS__)

#else

#define printf(...)

#endif
