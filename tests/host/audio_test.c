/* Exercise the actual port OpenSL worker with a deterministic audout backend. */
#include "../../source/opensles.c"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int mode, submits, callbacks;
static SLObj *player;
static int16_t *retired;
static int16_t fresh[4096];
static atomic_int entered, release_callback, destroy_returned;

void debugPrintf(const char *format, ...) { (void)format; }
u64 armGetSystemTick(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (u64)t.tv_sec * 1000000000 + t.tv_nsec;
}
u64 armTicksToNs(u64 ticks) { return ticks; }
void svcSleepThread(int64_t ns) {
  struct timespec t = {ns / 1000000000, ns % 1000000000};
  nanosleep(&t, NULL);
}
int rt_audout_open(void) { return 0; }
unsigned rt_audout_rate(void) { return 48000; }
void rt_audout_cancel(int on) { (void)on; }
void rt_audout_selftest(void) {}
void rt_audout_stats(RtAudoutStats *stats) { memset(stats, 0, sizeof *stats); }
int tasm2_audio_unlock(void *cb, void *ctx, void **held) {
  (void)cb;
  (void)ctx;
  *held = NULL;
  return 0;
}
void tasm2_audio_relock(void *held, int depth) {
  (void)held;
  (void)depth;
}
int rt_audout_submit(const int16_t *pcm) {
  ++submits;
  if (mode == 1 && submits == 1) {
    assert(pcm[0] == 111);
    assert(q_Clear(&player->bq) == SL_RESULT_SUCCESS);
    free(retired);
    retired = NULL;
    assert(q_Enqueue(&player->bq, fresh, sizeof fresh) == SL_RESULT_SUCCESS);
  }
  return 0;
}
static void completed(const void *queue, void *ctx) {
  (void)ctx;
  ++callbacks;
  assert(queue == &player->bq);
  if (mode == 1) {
    SLuint32 state[2];
    q_GetState(queue, state);
    assert(state[0] == 0 &&
           state[1] == 1); /* Clear replaced the old queue; only fresh playback completes. */
    g_closing = 1;
  } else if (mode == 2) {
    if (callbacks == 1)
      assert(q_Enqueue(queue, fresh, sizeof fresh) == SL_RESULT_SUCCESS);
    else
      g_closing = 1;
  } else if (mode == 3) {
    atomic_store(&entered, 1);
    while (!atomic_load(&release_callback))
      svcSleepThread(1000000);
  }
}
static void reset_player(int test_mode) {
  mode = test_mode;
  submits = callbacks = 0;
  g_closing = g_paused = g_in_callback = 0;
  g_out_n = 0;
  g_pos = 0;
  g_writes = 0;
  g_frames_in = 0;
  memset(g_prev, 0, sizeof g_prev);
  player = new_obj(K_PLAYER);
  assert(player);
  player->qcap = 2;
  player->rate = 48000;
  player->bits = 16;
  player->channels = 2;
  for (unsigned i = 0; i < sizeof fresh / sizeof *fresh; i++)
    fresh[i] = 222;
  assert(q_RegisterCallback(&player->bq, completed, NULL) == SL_RESULT_SUCCESS);
  assert(p_SetPlayState(&player->play, SL_PLAYSTATE_PLAYING) == SL_RESULT_SUCCESS);
}
static void retire_player(void) {
  pthread_mutex_destroy(&player->lock);
  pthread_cond_destroy(&player->cv);
  free(player);
  g_player = NULL;
}
static void *run_worker(void *arg) {
  audio_thread(arg);
  return NULL;
}
static void *release_later(void *arg) {
  (void)arg;
  svcSleepThread(10000000);
  assert(
      !atomic_load(&destroy_returned)); /* Destroy must still be waiting on the native callback. */
  atomic_store(&release_callback, 1);
  return NULL;
}
int main(void) {
  reset_player(1);
  retired = malloc(sizeof fresh);
  assert(retired);
  for (unsigned i = 0; i < sizeof fresh / sizeof *fresh; i++)
    retired[i] = 111;
  assert(q_Enqueue(&player->bq, retired, sizeof fresh) == SL_RESULT_SUCCESS);
  audio_thread(NULL);
  assert(!retired && callbacks == 1 && submits >= 2);
  retire_player();
  reset_player(2);
  audio_thread(NULL);
  assert(callbacks == 2 && submits > 0);
  retire_player();
  reset_player(3);
  atomic_store(&entered, 0);
  atomic_store(&release_callback, 0);
  atomic_store(&destroy_returned, 0);
  assert(q_Enqueue(&player->bq, fresh, sizeof fresh) == SL_RESULT_SUCCESS);
  pthread_t worker, releaser;
  assert(!pthread_create(&worker, NULL, run_worker, NULL));
  while (!atomic_load(&entered))
    svcSleepThread(1000000);
  assert(!pthread_create(&releaser, NULL, release_later, NULL));
  o_Destroy(&player->obj);
  atomic_store(&destroy_returned, 1);
  g_closing = 1;
  pthread_join(releaser, NULL);
  pthread_join(worker, NULL);
  assert(player->dead && !g_in_callback && callbacks == 1);
  retire_player();
  puts("audio: copied guest PCM, Clear generation, dry refill and callback teardown passed");
  return 0;
}
