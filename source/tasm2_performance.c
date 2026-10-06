/* Audited 1.2.8d performance choices. android32 file mmap is a heap copy,
 * so use the native ordinary-heap fallback instead of 64 MiB SD dump arenas.
 * RTA_ScaleRatio is the engine's own render-target scaling path; Android view,
 * input coordinates, presentation, medium detail settings and game time stay.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
int tasm2_performance_patch(uint8_t *image, size_t size, float scale) {
  static const uint32_t mmap_init[] = {
    0xe1a00005u, 0xe1a0100bu, 0xe3a02001u, 0xeb08719fu,
    0xe59f2514u, 0xe28d1e73u, 0xe59d3850u, 0xe7972002u, 0xe5c20000u
  };
  static const uint32_t alloc_gate[] = {
    0xe59f3098u, 0xe08f3003u, 0xe5d33008u, 0xe3530000u, 0x0a00000au
  };
  static const uint32_t free_gate[] = {
    0xe59f30a0u, 0xe08f3003u, 0xe5d33008u, 0xe3530000u, 0x1a000002u
  };
  static const uint32_t render[] = {
    0xe3a025feu, 0xe1a00008u, 0xeb0878f8u, 0xee080a10u,
    0xe1a01006u, 0xe59d0088u, 0xe240000cu, 0xeb34ad8bu
  };
  static const uint32_t scale_math[] = {
    0xee06aa90u, 0xeef87ae6u, 0xee069a90u, 0xeeb87ae6u,
    0xee687a27u, 0xee288a07u, 0xeefd7ae7u, 0xeebd8ac8u,
    0xee17aa90u, 0xee189a10u
  };
  if (!image || size < 0x11aac25u || !(scale >= 0.75f && scale <= 1.0f) ||
      memcmp(image + 0x3fd9fc, mmap_init, sizeof mmap_init) ||
      memcmp(image + 0x1a1d18, alloc_gate, sizeof alloc_gate) ||
      memcmp(image + 0x1a5a54, free_gate, sizeof free_gate) ||
      memcmp(image + 0x3fbcac, render, sizeof render) ||
      memcmp(image + 0x3fbcf8, scale_math, sizeof scale_math) ||
      memcmp(image + 0x11aac14, "MemoryEnableMMap", 17) ||
      memcmp(image + 0x11a4824, "RTA_ScaleRatio", 15)) return -1;
  uint32_t bits;
  memcpy(&bits, &scale, sizeof bits);
  /* ARM MOVW/MOVT r0 supply float bits using the original softfp return ABI.
   * The original VFP comparison, dimension conversion and RTA setup run.
   * No callback or code cave is needed; all checks precede all writes.
   */
  uint32_t scaled[] = {
    0xe3000000u | ((bits & 0xf000u) << 4) | (bits & 0xfffu),
    0xe3400000u | ((bits & 0xf0000000u) >> 12) | ((bits >> 16) & 0xfffu),
    0xe1a00000u
  };
  uint32_t no_mmap = 0xe3a00000u;
  memcpy(image + 0x3fda08, &no_mmap, sizeof no_mmap);
  memcpy(image + 0x3fbcac, scaled, sizeof scaled);
  return 0;
}
