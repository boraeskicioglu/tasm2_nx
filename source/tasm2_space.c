/* Query Horizon's mounted SD filesystem in bytes, without libc counters. */
#include "util.h"
#include <errno.h>
#include <stdint.h>
#include <stdatomic.h>
#include <switch.h>

int tasm2_sd_space(uint64_t *available, uint64_t *total) {
  FsFileSystem *fs = fsdevGetDeviceFileSystem("sdmc");
  s64 free_bytes = 0, total_bytes = 0;
  Result rc = fs ? fsFsGetFreeSpace(fs, "/", &free_bytes) : 0;
  if (fs && R_SUCCEEDED(rc) && total)
    rc = fsFsGetTotalSpace(fs, "/", &total_bytes);
  if (!fs || R_FAILED(rc) || free_bytes < 0 ||
      (total && (total_bytes < 0 || free_bytes > total_bytes))) {
    static atomic_uint errors;
    if (atomic_fetch_add_explicit(&errors, 1, memory_order_relaxed) < 4)
      debugPrintf("[space] SD capacity query failed: mounted=%d result=0x%x\n", fs != NULL,
                  (unsigned)rc);
    errno = EIO;
    return -1;
  }
  *available = (uint64_t)free_bytes;
  if (total)
    *total = (uint64_t)total_bytes;
  static atomic_flag reported = ATOMIC_FLAG_INIT;
  if (!atomic_flag_test_and_set_explicit(&reported, memory_order_relaxed))
    debugPrintf("[space] SD available: %llu bytes (%.2f MiB), Horizon 64-bit query\n",
                (unsigned long long)*available, (double)*available / 1048576.0);
  return 0;
}
float tasm2_free_disk_mb(void) {
  uint64_t available;
  return tasm2_sd_space(&available, NULL) ? 0.0f : (float)((double)available / 1048576.0);
}
float tasm2_free_disk_kb(void) {
  uint64_t available;
  return tasm2_sd_space(&available, NULL) ? 0.0f : (float)((double)available / 1024.0);
}
