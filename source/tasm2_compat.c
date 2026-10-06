/* Original UTF-8 and wide-character ABI adapters. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <switch.h>
size_t b_mbrtowc(uint32_t *, const char *, size_t, void *);
size_t b_mbstowcs(uint32_t *dst, const char *src, size_t cap) {
  uint8_t state[4] = {0};
  size_t count = 0, remaining = strlen(src) + 1;
  while (!dst || count < cap) {
    uint32_t wc;
    size_t used = b_mbrtowc(&wc, src, remaining, state);
    if (used == (size_t)-1 || used == (size_t)-2)
      return (size_t)-1;
    if (dst)
      dst[count] = wc;
    if (!wc)
      return count;
    src += used;
    remaining -= used;
    count++;
  }
  return count;
}
int b_wcscmp(const uint32_t *a, const uint32_t *b) {
  while (*a && *a == *b)
    a++, b++;
  return *a < *b ? -1 : *a > *b;
}
uint32_t *b_wcscpy(uint32_t *a, const uint32_t *b) {
  uint32_t *out = a;
  do {
    *a++ = *b;
  } while (*b++);
  return out;
}
/* FAT has no Unix mode bits; retain the process mask and its return value. */
unsigned b_umask(unsigned mask) {
  static Mutex lock;
  static unsigned current = 022;
  mutexLock(&lock);
  unsigned old = current;
  current = mask & 0777;
  mutexUnlock(&lock);
  return old;
}
