#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int tasm2_disable_space_warning(uint8_t *, size_t);
void debugPrintf(const char *fmt, ...) { (void)fmt; }
static void words(uint8_t *image, size_t offset, const uint32_t *value, size_t count) {
  memcpy(image + offset, value, count * sizeof *value);
}
int main(void) {
  const size_t size = 0x11a4434;
  uint8_t *image = calloc(1, size), *original = malloc(size);
  assert(image && original);
  const uint32_t entry[] = {0xe59f3240u, 0xe92d40f0u, 0xe1a04000u, 0xe24dd01cu};
  const uint32_t threshold[] = {0xe35500ffu, 0xda000029u};
  const uint32_t query = 0xebff51b8u, format_size = 0xe3a02c01u, scale = 0x3a800000u;
  words(image, 0x3f2030, entry, 4); words(image, 0x3f205c, &query, 1);
  words(image, 0x3f20a8, threshold, 2); words(image, 0x3f21e4, &format_size, 1);
  words(image, 0x3c67f4, &scale, 1);
  memcpy(image + 0x11a4420, "UI_MemoryNotEnough", 18);
  memcpy(original, image, size);
  assert(tasm2_disable_space_warning(NULL, size) == -1);
  assert(tasm2_disable_space_warning(image, 0x11a4431) == -1);
  const size_t guards[] = {0x3f2030, 0x3f203c, 0x3f205c, 0x3f20a8,
                          0x3f20ac, 0x3f21e4, 0x3c67f4, 0x11a4420, 0x11a4431};
  for (size_t n = 0; n < sizeof guards / sizeof guards[0]; n++) {
    image[guards[n]] ^= 1;
    assert(tasm2_disable_space_warning(image, size) == -1);
    image[guards[n]] ^= 1;
    assert(!memcmp(image, original, size));
  }
  assert(tasm2_disable_space_warning(image, size) == 0);
  uint32_t instruction;
  memcpy(&instruction, image + 0x3f2030, 4);
  assert(instruction == 0xe12fff1eu);
  assert(!memcmp(image, original, 0x3f2030));
  assert(!memcmp(image + 0x3f2034, original + 0x3f2034, size - 0x3f2034));
  free(image); free(original);
  puts("warning: production version guards reject altered/short images without publication; only the ARM preflight entry becomes bx lr");
  return 0;
}
