/* Offline Switch notifications, verified against original Android 1.2.8d.
 * Only reviewed call sites are redirected. The game's shared dialog builders
 * and gameplay confirmations remain intact. Callbacks run after native step,
 * when the caller has released its temporary strings and finished its state.
 */
#include "tasm2_popups.h"
#include "error.h"
#include "util.h"
#include <string.h>
#include <switch.h>

typedef struct {
  uint32_t pc, key_offset;
  unsigned mode;
  uint32_t before[4];
  const char *key;
} PopupSite;
#include "tasm2_popup_sites.h"

#define CAVE 0x3f2034u
#define COUNT (sizeof popup_sites / sizeof popup_sites[0])
int tasm2_popups_patch(uint8_t *image, size_t size, uint32_t enqueue) {
  /* This region is solely the body of the disabled capacity-warning routine.
   * Its entry must already be bx lr. Private-ELF regression checks ensure no
   * outside code/relocation enters the reused body. No runtime code allocation
   * or executable-heap assumption is needed: android32 seals this staged text.
   */
  static const uint32_t original[20] = {
    0xe92d40f0u, 0xe1a04000u, 0xe24dd01cu, 0xe79f3003u, 0xe5d33000u,
    0xe3530000u, 0x1a000001u, 0xe28dd01cu, 0xe8bd80f0u, 0xe59f621cu,
    0xebff51b8u, 0xe1a05000u, 0xe08f6006u, 0xe5963000u, 0xe3130001u,
    0x0a000027u, 0xe59f6204u, 0xe08f6006u, 0xe3560000u, 0x0a000008u
  };
  static const uint32_t builder_offsets[] = {0x20e48cu, 0x20ea3cu, 0x20c5f0u};
  static const uint32_t builders[][4] = {
    {0xe59fc57cu, 0xe92d4ff0u, 0xe1a09000u, 0xed2d8b02u},
    {0xe59fc5ccu, 0xe92d4ff0u, 0xe1a09000u, 0xed2d8b02u},
    {0xe92d4ff0u, 0xe1a04000u, 0xe59f72fcu, 0xe24dd074u}
  };
  /* Offline FIRST_CHECK is generated again while the legacy cloud state is
   * pending. Its OK callback selects UI state 2, recreating the menu each frame.
   * Follow the existing normal menu dispatch at 0x433ee4 instead of entering
   * that obsolete network preflight. Do not fabricate connected/cloud state.
   */
  static const uint32_t gate[] = {0xe5d33b52u, 0xe3530000u, 0x1affffe3u, 0xe59f31acu};
  static const uint32_t predicate[] = {0xe5d42310u, 0xe3520000u, 0x1a0001eau};
  static const uint32_t dispatch[] = {0xe5943074u, 0xe3530000u, 0x0a000405u,
                                     0xe2433002u, 0xe3530011u, 0x908ff103u, 0xea0006b8u};
  const uint32_t returned = 0xe12fff1eu;
  if (!image || size <= 0x1352321 || !enqueue ||
      memcmp(image + CAVE - 4, &returned, 4) ||
      memcmp(image + CAVE, original, sizeof original) ||
      memcmp(image + 0x433f48, gate, sizeof gate) ||
      memcmp(image + 0x433790, predicate, sizeof predicate) ||
      memcmp(image + 0x433ee4, dispatch, sizeof dispatch))
    return -1;
  for (size_t n = 0; n < 3; n++)
    if (memcmp(image + builder_offsets[n], builders[n], sizeof builders[n]))
      return -1;
  /* Validate the complete set before publishing any call-site or stub. */
  for (size_t n = 0; n < COUNT; n++) {
    const PopupSite *s = &popup_sites[n];
    if (memcmp(image + s->pc - 12, s->before, sizeof s->before) ||
        memcmp(image + s->key_offset, s->key, strlen(s->key) + 1))
      return -1;
  }
  for (unsigned mode = 0; mode < 4; mode++) {
    /* Existing BL retains LR and stack. Load the original dialog callback
     * and userdata, add the reviewed choice, then tail-call the port queue.
     * ldr pc interworks with an ARM or Thumb C entry. No native callback runs
     * inside the old builder call, so transitions cannot destroy its caller.
     * Update-details uniquely supplies callback in r3 and userdata at sp+0.
     */
    uint32_t stub[5] = {0xe59d0000u, 0xe59d1004u,
                       0xe3a02000u | mode, 0xe51ff004u, enqueue};
    if (mode == 3) {
      stub[0] = 0xe1a00003u; /* mov r0,r3 */
      stub[1] = 0xe59d1000u; /* ldr r1,[sp] */
      stub[2] = 0xe3a02000u; /* choice 0 */
    }
    memcpy(image + CAVE + mode * sizeof stub, stub, sizeof stub);
  }
  for (size_t n = 0; n < COUNT; n++) {
    const PopupSite *s = &popup_sites[n];
    const int32_t delta = (int32_t)(CAVE + s->mode * 20) - (int32_t)(s->pc + 8);
    const uint32_t bl = s->mode == 4 ? 0xe1a00000u :
                        0xeb000000u | (((uint32_t)delta >> 2) & 0xffffffu);
    memcpy(image + s->pc, &bl, sizeof bl);
  }
  const uint32_t normal_menu_dispatch = 0xeaffffe3u; /* b 0x433ee4 */
  memcpy(image + 0x433f50, &normal_menu_dispatch, sizeof normal_menu_dispatch);
  debugPrintf("[popup] checked FIRST_CHECK preflight bypass; native menu dispatch preserved\n");
  debugPrintf("[popup] %u checked offline/network/cloud/update/social notification sites suppressed\n",
              (unsigned)COUNT);
  return 0;
}

#define QUEUE_CAPACITY 256
static struct PendingPopup {
  Tasm2PopupCallback callback;
  void *userdata;
  int choice;
} pending[QUEUE_CAPACITY];
static unsigned pending_count;
static Mutex pending_lock;
void tasm2_popups_init(void) {
  mutexInit(&pending_lock);
  pending_count = 0;
}
void tasm2_popups_enqueue(Tasm2PopupCallback callback, void *userdata, int choice) {
  if (!callback)
    return;
  mutexLock(&pending_lock);
  for (unsigned n = 0; n < pending_count; n++)
    if (pending[n].callback == callback && pending[n].userdata == userdata &&
        pending[n].choice == choice) {
      mutexUnlock(&pending_lock);
      return;
    }
  if (pending_count == QUEUE_CAPACITY) {
    mutexUnlock(&pending_lock);
    fatal_error("Offline notification callback queue exhausted. See debug.log.");
    return;
  }
  pending[pending_count++] = (struct PendingPopup){callback, userdata, choice};
  mutexUnlock(&pending_lock);
}
void tasm2_popups_tick(void) {
  struct PendingPopup ready[QUEUE_CAPACITY];
  mutexLock(&pending_lock);
  const unsigned count = pending_count;
  memcpy(ready, pending, count * sizeof *ready);
  pending_count = 0;
  mutexUnlock(&pending_lock);
  /* Call outside the lock. A callback can enqueue another notification; that
   * continuation waits for the next frame instead of recursively transitioning.
   */
  for (unsigned n = 0; n < count; n++) {
    debugPrintf("[popup] native continuation cb=%p data=%p choice=%d\n",
                (void *)ready[n].callback, ready[n].userdata, ready[n].choice);
    ready[n].callback(ready[n].choice, ready[n].userdata);
  }
}
