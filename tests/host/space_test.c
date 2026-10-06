#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>
int tasm2_sd_space(uint64_t *, uint64_t *);
float tasm2_free_disk_mb(void), tasm2_free_disk_kb(void);
static FsFileSystem sd = {1};
static s64 available, capacity;
static Result free_result, total_result;
static unsigned free_calls, total_calls;
void debugPrintf(const char *fmt, ...) { (void)fmt; }
FsFileSystem *fsdevGetDeviceFileSystem(const char *name) {
  assert(!strcmp(name, "sdmc"));
  return sd.mounted ? &sd : NULL;
}
Result fsFsGetFreeSpace(FsFileSystem *fs, const char *path, s64 *out) {
  assert(fs == &sd && !strcmp(path, "/"));
  free_calls++;
  if (!free_result)
    *out = available;
  return free_result;
}
Result fsFsGetTotalSpace(FsFileSystem *fs, const char *path, s64 *out) {
  assert(fs == &sd && !strcmp(path, "/"));
  total_calls++;
  if (!total_result)
    *out = capacity;
  return total_result;
}
int main(void) {
  capacity = INT64_C(512) << 30;
  uint64_t free_bytes = 0, total_bytes = 0;
  available = (INT64_C(20) << 30) + (INT64_C(128) << 20);
  assert(tasm2_sd_space(&free_bytes, &total_bytes) == 0);
  assert(free_bytes == (uint64_t)available && total_bytes == (uint64_t)capacity);
  assert(free_bytes > UINT32_MAX); /* Preserve values above 4 GiB. */
  assert(tasm2_free_disk_mb() == 20608.0f);
  assert(tasm2_free_disk_kb() == 20608.0f * 1024.0f);
  for (unsigned mb = 255; mb <= 257; mb++) {
    available = (s64)mb << 20;
    assert(tasm2_free_disk_mb() == (float)mb);
  }
  available = 0;
  assert(tasm2_free_disk_mb() == 0.0f);
  available = INT64_C(8) << 30;
  assert(tasm2_free_disk_mb() == 8192.0f);
  unsigned calls = total_calls;
  assert(tasm2_sd_space(&free_bytes, NULL) == 0 && total_calls == calls);
  free_result = 0x1234;
  free_bytes = 123;
  assert(tasm2_sd_space(&free_bytes, NULL) == -1 && errno == EIO && free_bytes == 123);
  assert(tasm2_free_disk_mb() == 0.0f);
  assert(total_calls == calls);
  free_result = 0;
  total_result = 0x5678;
  assert(tasm2_sd_space(&free_bytes, &total_bytes) == -1 && errno == EIO);
  total_result = 0;
  available = -1;
  assert(tasm2_sd_space(&free_bytes, NULL) == -1);
  available = capacity + 1;
  assert(tasm2_sd_space(&free_bytes, &total_bytes) == -1);
  sd.mounted = 0;
  calls = free_calls;
  assert(tasm2_free_disk_mb() == 0.0f && free_calls == calls);
  puts("space: production Horizon query preserves >4 GiB, MB/KB units, 256 MiB boundary, real zero and query failures");
  return 0;
}
