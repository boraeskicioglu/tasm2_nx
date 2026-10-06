/* Version-checked 1.2.8d fixes, verified against the owner's ELF disassembly. */
#include "error.h"
#include "tasm2.h"
#include "tasm2_popups.h"
#include "util.h"
#include <stdint.h>
#include <string.h>
extern float tasm2_render_scale;
int tasm2_performance_patch(uint8_t *, size_t, float);
int tasm2_unlock_fps(uint8_t *, size_t);
int tasm2_patch_scaled_tick(uint8_t *, size_t, uint32_t);
int tasm2_scaled_tick(uint32_t);
int tasm2_shop_patch(uint8_t *, size_t);
int tasm2_checkpoint_patch(uint8_t *, size_t);
void tasm2_checkpoint_bind(uintptr_t);
void tasm2_shop_bind(uintptr_t),tasm2_shop_tick(void);
int tasm2_disable_space_warning(uint8_t *, size_t);
void tasm2_fixes_prepare(so_module *m) {
  uint8_t *image = m->load_base;
  static const uint32_t before[] = {0xebffff9eu, 0xe5967064u, 0xe3570000u, 0x0a000030u};
  /* Move the destructor's null test before Clear(), preserving branch targets. */
  static const uint32_t after[] = {0xe5967064u, 0xe3570000u, 0x0a000031u, 0xebffff9bu};
  if (m->load_size <= 0x1352321 || memcmp(image + 0x419a60, before, sizeof before) ||
      tasm2_shop_patch(image, m->load_size) ||
      tasm2_checkpoint_patch(image, m->load_size) ||
      tasm2_unlock_fps(image, m->load_size) ||
      tasm2_performance_patch(image, m->load_size, tasm2_render_scale) ||
      tasm2_disable_space_warning(image, m->load_size) ||
      tasm2_popups_patch(image, m->load_size, (uint32_t)(uintptr_t)tasm2_popups_enqueue) ||
      tasm2_patch_scaled_tick(image, m->load_size, (uint32_t)(uintptr_t)tasm2_scaled_tick))
    fatal_error("Unsupported TASM2 native layout. Use the original 1.2.8d APK.");
  memcpy(image + 0x419a60, after, sizeof after);
  tasm2_popups_init();
  tasm2_shop_bind((uintptr_t)m->load_virtbase);
  tasm2_checkpoint_bind((uintptr_t)m->load_virtbase);
  debugPrintf("[perf] native RAM allocator; SD mmap dump arenas disabled; RTA scale %.2f, 1280x720 presentation\n", (double)tasm2_render_scale);
  debugPrintf("[save] checked native local progression and first offline SaveIndex enabled\n");
  debugPrintf("[perf] checked native FPSLimit sleep bypass; 60 Hz vsync, real game time retained\n");
  debugPrintf("[perf] fractional scaled milliseconds retained; 60 Hz slow-motion/finisher progression\n");
  debugPrintf("[shop] checked native CRM local initialization; local OBB catalog and offline costume inventory enabled\n");
  debugPrintf("[compat] checked resource destructor; offline notification continuations enabled\n");
}
void tasm2_fixes_tick(void) {
  tasm2_popups_tick();
  tasm2_shop_tick();
}
