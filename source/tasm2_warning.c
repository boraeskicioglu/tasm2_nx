/* Disable only the original 1.2.8d startup capacity-warning routine.
 * The user's request is to omit this preflight, including its capacity query.
 * All preimages below were checked in this game's original ARMv7 ELF. */
#include "util.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
int tasm2_disable_space_warning(uint8_t *image, size_t size) {
  static const uint32_t entry[] = {0xe59f3240u, 0xe92d40f0u, 0xe1a04000u, 0xe24dd01cu};
  static const uint32_t threshold[] = {0xe35500ffu, 0xda000029u};
  static const uint32_t query = 0xebff51b8u, format_size = 0xe3a02c01u;
  static const uint32_t kb_to_mb = 0x3a800000u;
  static const char message[] = "UI_MemoryNotEnough";
  if (!image || size < 0x11a4420 + sizeof message ||
      memcmp(image + 0x3f2030, entry, sizeof entry) ||
      memcmp(image + 0x3f205c, &query, sizeof query) ||
      memcmp(image + 0x3f20a8, threshold, sizeof threshold) ||
      memcmp(image + 0x3f21e4, &format_size, sizeof format_size) ||
      memcmp(image + 0x3c67f4, &kb_to_mb, sizeof kb_to_mb) ||
      memcmp(image + 0x11a4420, message, sizeof message))
    return -1;
  /* This is an ARM void routine. Both verified callers ignore r0, and its
   * only work is querying capacity and creating/clearing the 256 MB modal.
   * Return before its prologue; leave SP, callee-saved regs and state intact.
   * The loader seals and flushes the staged image after all port fixes. */
  const uint32_t return_to_caller = 0xe12fff1eu; /* bx lr (interworks) */
  memcpy(image + 0x3f2030, &return_to_caller, sizeof return_to_caller);
  debugPrintf("[compat] checked 1.2.8d 256 MB startup warning disabled (no preflight query)\n");
  return 0;
}
