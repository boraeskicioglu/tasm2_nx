/* Log and flush native writable streams through android32's own I/O shims. */
#ifndef HOST_SAVE_TEST
#include "bionic.h"
#include "imports.h"
#endif
#include "tasm2_obb.h"
#include "dcr_path.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <unistd.h>
void *b_fopen(const char *, const char *);
int b_fclose(void *), b_fflush(void *);
static Mutex save_lock;
static struct Writer {
  void *stream;
  int durable;
  struct Writer *next;
} *writers;
/* gv3 is Gameloft's analytics queue. fclose still writes these files, but an
 * SD fsync for every timer/token event is unnecessary in the offline port.
 * Prefix includes both separators: gv30 and save subdirectories stay durable.
 */
static int durable_path(const char *real) {
  const char *relative = real + strlen(dcr_game_root());
  return strncmp(relative, "/data/files/gv3/", 16) != 0;
}
static unsigned logged_writers;
static void *save_open(const char *path, const char *mode) {
  int handled;
  void *f = tasm2_obb_open(path, mode, &handled);
  if (!handled)
    f = b_fopen(path, mode);
  else if (!f)
    b_fix_errno();
  if (f && mode && strpbrk(mode, "wa+")) {
    char mapped[DCR_PATH_MAX];
    const char *real = dcr_translate_path(path, mapped, sizeof mapped);
    size_t n = strlen(dcr_game_root());
    if (!strncmp(real, dcr_game_root(), n) && real[n] == '/') {
      struct Writer *w = malloc(sizeof *w);
      if (w) {
        w->stream = f;
        w->durable = durable_path(real);
        mutexLock(&save_lock);
        w->next = writers;
        writers = w;
        int log = w->durable && logged_writers++ < 16;
        mutexUnlock(&save_lock);
        if (log)
          debugPrintf("[save] durable native file: %s (%s)\n", real, mode);
      }
    }
  }
  return f;
}
static int flush_stream(void *stream) {
  int r = b_fflush(stream);
  if (!r && fsync(fileno((FILE *)stream)))
    r = -1;
  if (r)
    debugPrintf("[save] native file flush failed; check SD free space\n");
  return r;
}
static int save_close(void *stream) {
  int writable = 0;
  mutexLock(&save_lock);
  for (struct Writer **slot = &writers; *slot; slot = &(*slot)->next) {
    if ((*slot)->stream == stream) {
      struct Writer *old = *slot;
      *slot = old->next;
      writable = old->durable;
      free(old);
      break;
    }
  }
  mutexUnlock(&save_lock);
  int r = writable ? flush_stream(stream) : 0;
  int closed = b_fclose(stream);
  return r ? r : closed;
}
void tasm2_save_flush(void) {
  mutexLock(&save_lock);
  for (struct Writer *w = writers; w; w = w->next)
    flush_stream(w->stream);
  mutexUnlock(&save_lock);
  fsdevCommitDevice("sdmc");
}
#ifndef HOST_SAVE_TEST
int b_statfs(const char *, struct b_statfs *);
int tasm2_sd_space(uint64_t *, uint64_t *);
static int sd_statfs(const char *path, struct b_statfs *out) {
  /* Keep android32's verified guest ABI and replace its fixed capacities with
   * the real SD counters. Android resource/internal paths all map to this SD. */
  if (!path || !out) {
    b_set_errno(L_EFAULT);
    return -1;
  }
  uint64_t available, total;
  if (tasm2_sd_space(&available, &total)) {
    b_set_errno(L_EIO);
    return -1;
  }
  if (b_statfs(path, out))
    return -1;
  out->f_blocks = total / out->f_bsize;
  out->f_bfree = out->f_bavail = available / out->f_bsize;
  return 0;
}
const DynLibFunction port_imports[] = {{"fopen", (uintptr_t)save_open},
                                       {"fclose", (uintptr_t)save_close},
                                       {"statfs", (uintptr_t)sd_statfs}};
const int port_imports_count = sizeof port_imports / sizeof port_imports[0];

#endif /* HOST_SAVE_TEST */
