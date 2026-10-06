/* Verified 1.2.8d FPSLimit timer: retain real elapsed time and its 100 ms
 * clamp, skip only the Android minimum-frame sleep. EGL interval 1 paces
 * presentation at 60 Hz; no physics/animation time constant is changed.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
int tasm2_unlock_fps(uint8_t *image, size_t size) {
  static const uint32_t before[] = {
    0xe5942328u, 0xe584133cu, 0xe5840338u, 0xe3520000u,
    0xe5943340u, 0xda00001fu, 0xe3550000u, 0x0a00001du,
    0xe3a01000u, 0xe0630000u, 0xe1500001u
  };
  static const uint32_t elapsed[] = {
    0xe0630000u, 0xe3500000u, 0xb3a00000u, 0xba000001u,
    0xe3500064u, 0xa3a00064u, 0xe5840324u, 0xe28dd014u, 0xe8bd8030u
  };
  if (!image || size < 0x3b1c8cu ||
      memcmp(image + 0x3b1bd0, before, sizeof before) ||
      memcmp(image + 0x3b1c68, elapsed, sizeof elapsed))
    return -1;
  uint32_t branch = 0xea00001fu; /* b 3b1c68: original elapsed-time path */
  memcpy(image + 0x3b1be4, &branch, sizeof branch);
  return 0;
}

/* 1.2.8d truncates elapsed_ms * time_scale to int at 0x3e9158. At 60 Hz,
 * slow motion below 1 ms/frame otherwise advances by zero forever. Carry the
 * discarded fraction between updates; real pauses and the 100 ms cap remain.
 * Called only by the engine's main update thread, as the original conversion.
 */
static float scaled_remainder;
int tasm2_scaled_tick(uint32_t bits) {
  float ms;
  memcpy(&ms, &bits, sizeof ms);
  if (!(ms >= 0.0f)) { scaled_remainder = 0; return 0; }
  if (ms >= 100.0f) { scaled_remainder = 0; return 100; }
  float total = ms + scaled_remainder;
  int whole = (int)total;
  scaled_remainder = total - whole;
  return whole;
}
int tasm2_patch_scaled_tick(uint8_t *image, size_t size, uint32_t handler) {
  enum { SITE = 0x3e9158, CAVE = 0x3f2084 };
  static const uint32_t before[] = {
    0xee278a88u, 0xeebd8ac8u, 0xee183a10u, 0xe1530004u,
    0xed888ac8u, 0xba000002u, 0xe3530064u, 0xb1a04003u,
    0xa3a04064u, 0xe5d8331cu, 0xe5884320u
  };
  static const uint32_t cave_before[] = {
    0xe59f71f8u, 0xe08f7007u, 0xe5973000u, 0xe3130001u,
    0x0a00000fu, 0xe59f31e8u, 0xe08f3003u, 0xe5933008u,
    0xe0855003u, 0xe35500ffu
  };
  const uint32_t returned = 0xe12fff1eu;
  if (!image || size < CAVE + sizeof cave_before || !handler ||
      memcmp(image + 0x3f2030, &returned, 4) ||
      memcmp(image + SITE - 4, before, sizeof before) ||
      memcmp(image + CAVE, cave_before, sizeof cave_before)) return -1;
  /* Preserve caller-saved integer registers/LR and 8-byte stack alignment.
   * AAPCS preserves d8/s16 across C. Only r3 and s16 change, as at the original
   * conversion. Native multiplication and downstream clamp/update stay intact.
   * This follows the existing popup stubs in the disabled warning's dead body.
   */
  uint32_t stub[] = {
    0xe92d500fu,                 /* push {r0-r3,ip,lr} */
    0xee180a10u,                 /* vmov r0,s16 (float bits) */
    0xe59fc014u,                 /* ldr ip,[pc,#20] -> final literal */
    0xe12fff3cu,                 /* blx ip */
    0xee080a10u,                 /* vmov s16,r0 (integer bits) */
    0xe8bd500fu,                 /* pop {r0-r3,ip,lr} */
    0xee183a10u,                 /* vmov r3,s16 */
    0xea000000u | (((uint32_t)(0x3e9160 - (CAVE + 28 + 8)) >> 2) & 0xffffffu),
    0xe1a00000u,                 /* padding */
    handler
  };
  uint32_t branch = 0xea000000u | (((uint32_t)(CAVE - (SITE + 8)) >> 2) & 0xffffffu);
  memcpy(image + CAVE, stub, sizeof stub);
  memcpy(image + SITE, &branch, 4);
  scaled_remainder = 0;
  return 0;
}
