/* Hand off only the verified TASM2 mixer mutex owned by the destroying thread.
 * Native callback +0xbb7c98 takes *(context+4) before inspecting/refilling its queue.
 */
#include "bionic_pthread.h"
#include "tasm2.h"
#include "util.h"
#include <stdint.h>
size_t dcr_readable(uint32_t, size_t);
int b_pthread_mutex_unlock(b_pthread_mutex_t *), b_pthread_mutex_lock(b_pthread_mutex_t *);
int tasm2_audio_unlock(void *callback, void *context, void **held) {
  *held = NULL;
  if ((uintptr_t)callback != (uintptr_t)tasm2_game.load_virtbase + 0xbb7c98 || !context ||
      dcr_readable((uint32_t)(uintptr_t)context, 8) < 8)
    return 0;
  b_pthread_mutex_t *pm = *(b_pthread_mutex_t **)((uint8_t *)context + 4);
  if (!pm || dcr_readable((uint32_t)(uintptr_t)pm, 4) < 4)
    return 0;
  BMutex *mx = (BMutex *)(uintptr_t)__atomic_load_n(&pm->value, __ATOMIC_ACQUIRE);
  if (dcr_readable((uint32_t)(uintptr_t)mx, sizeof *mx) < sizeof *mx || mx->magic != 0x4d555458u)
    return 0;
  int depth = 1;
  if (mx->type == B_PTHREAD_MUTEX_NORMAL) {
    if ((__atomic_load_n((uint32_t *)&mx->m, __ATOMIC_ACQUIRE) & 0x3fffffffu) !=
        threadGetCurHandle())
      return 0;
  } else {
    if (mx->owner != (uintptr_t)b_thread_self() || mx->count < 1 || mx->count > 32)
      return 0;
    depth = mx->count;
  }
  for (int i = 0; i < depth; i++)
    b_pthread_mutex_unlock(pm);
  *held = pm;
  debugPrintf("[audio] temporarily handed owned mixer mutex to completion callback\n");
  return depth;
}
void tasm2_audio_relock(void *held, int depth) {
  for (int i = 0; i < depth; i++)
    b_pthread_mutex_lock(held);
}
