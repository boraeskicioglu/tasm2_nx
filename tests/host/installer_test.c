#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void tasm2_installer_init(uint8_t *, size_t);
static jmp_buf failure;
static uint32_t cells[4];
static unsigned attempts, frees, fail_at;
void *host_malloc(size_t size) {
  assert(size == 4 && attempts < 4);
  unsigned i = attempts++;
  return attempts == fail_at ? NULL : &cells[i];
}
void host_free(void *p) {
  assert(p >= (void *)cells && p < (void *)(cells + 4));
  frees++;
}
void fatal_error(const char *fmt, ...) {
  (void)fmt;
  longjmp(failure, 1);
}
void debugPrintf(const char *fmt, ...) { (void)fmt; }
static const uint32_t state[] = {0x13b7b30, 0x13b7b34, 0x13b7b38, 0x13b7b3c};
static const uint32_t got[] = {0x133a8bc, 0x133a8b4, 0x133a8c0, 0x133a8b8};
static void reset(uint8_t *base) {
  const uint32_t stores[] = {0xe7972002, 0xe5922000, 0xe5823000,
                             0xe7972001, 0xe5922000, 0xe5823000};
  const uint32_t offsets[] = {0xffffeecc, 0xffffeed0, 0xffffeef8, 0xffffeefc};
  memset(base, 0, 0x13b7b40);
  memcpy(base + 0x3fd4ec, stores, sizeof stores);
  memcpy(base + 0x3fded0, offsets, sizeof offsets);
  for (unsigned i = 0; i < 4; i++)
    *(uint32_t *)(base + got[i]) = (uint32_t)(uintptr_t)(base + state[i]);
  attempts = frees = fail_at = 0;
  memset(cells, 0, sizeof cells);
}
static void expect_failure(uint8_t *base, size_t size) {
  if (!setjmp(failure)) {
    tasm2_installer_init(base, size);
    assert(!"Expected a guarded failure");
  }
}
int main(void) {
  uint8_t *base = calloc(1, 0x13b7b40);
  assert(base);
  reset(base);
  tasm2_installer_init(base, 0x13b7b40);
  assert(attempts == 4 && frees == 0);
  for (unsigned i = 0; i < 4; i++) {
    assert(cells[i] == 1);
    assert(*(uint32_t *)(base + state[i]) == (uint32_t)(uintptr_t)&cells[i]);
  }
  cells[1] = cells[3] = 0; /* Engine changes must survive repeated preparation. */
  tasm2_installer_init(base, 0x13b7b40);
  assert(attempts == 4 && cells[1] == 0 && cells[3] == 0);
  reset(base);
  *(uint32_t *)(base + state[0]) = 1;
  expect_failure(base, 0x13b7b40);
  assert(attempts == 0);
  reset(base);
  *(uint32_t *)(base + got[2]) ^= 4;
  expect_failure(base, 0x13b7b40);
  assert(attempts == 0);
  reset(base);
  base[0x3fd4f4] ^= 1;
  expect_failure(base, 0x13b7b40);
  assert(attempts == 0);
  reset(base);
  expect_failure(base, 0x13b7b3f);
  assert(attempts == 0);
  reset(base);
  fail_at = 3;
  expect_failure(base, 0x13b7b40);
  assert(attempts == 3 && frees == 2);
  for (unsigned i = 0; i < 4; i++)
    assert(*(uint32_t *)(base + state[i]) == 0);
  free(base);
  puts("installer: production initialization, repeat preservation, layout/relocation/partial guards and allocation rollback passed");
  return 0;
}
