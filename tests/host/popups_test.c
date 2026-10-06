#include "tasm2_popups.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned calls;
static int last_choice;
static void *last_userdata;
void debugPrintf(const char *fmt, ...) { (void)fmt; }
void fatal_error(const char *fmt, ...) { (void)fmt; abort(); }
static void acknowledged(int choice, void *userdata) {
  calls++; last_choice = choice; last_userdata = userdata;
}
static void chained(int choice, void *userdata) {
  assert(choice == 1);
  calls++;
  tasm2_popups_enqueue(acknowledged, userdata, 2);
}
static void *producer(void *data) {
  for (unsigned i = 0; i < 1000; i++)
    tasm2_popups_enqueue(acknowledged, data, 0);
  return NULL;
}
int main(void) {
  int userdata;
  tasm2_popups_init();
  tasm2_popups_enqueue(NULL, &userdata, 0);
  tasm2_popups_enqueue(acknowledged, &userdata, 0);
  tasm2_popups_enqueue(acknowledged, &userdata, 0);
  assert(calls == 0); /* The builder's caller must finish before continuation. */
  tasm2_popups_tick();
  assert(calls == 1 && last_choice == 0 && last_userdata == &userdata);
  tasm2_popups_tick(); assert(calls == 1);
  tasm2_popups_enqueue(chained, &userdata, 1);
  tasm2_popups_tick(); assert(calls == 2);
  tasm2_popups_tick(); assert(calls == 3 && last_choice == 2);
  pthread_t threads[4];
  for (unsigned i = 0; i < 4; i++) assert(!pthread_create(&threads[i], NULL, producer, &userdata));
  for (unsigned i = 0; i < 4; i++) assert(!pthread_join(threads[i], NULL));
  assert(calls == 3); tasm2_popups_tick(); assert(calls == 4);
  /* Capacity comfortably exceeds all static sites; every distinct userdata
   * receives its continuation, with no accidental global deduplication. */
  int distinct[256];
  for (unsigned i = 0; i < 256; i++) tasm2_popups_enqueue(acknowledged, &distinct[i], 1);
  tasm2_popups_tick(); assert(calls == 260);
  tasm2_popups_tick(); assert(calls == 260);
  assert(tasm2_popups_patch(NULL, 0, 1) == -1);
  puts("popups: deferred once-only continuations, preserved choice/userdata, reentrant next-frame transition, concurrent dedup and 256 distinct acknowledgements pass");
  return 0;
}
