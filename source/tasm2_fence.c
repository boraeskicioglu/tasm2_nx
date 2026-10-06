/* NV fences backed by real GLES3 sync objects on the shared Mesa contexts.
 * Polls never wait for unrelated later GPU work. GLES2 fallback completes
 * once at SetFence, then remembers that completion until reset/deletion.
 */
#include "util.h"
#include "error.h"
#include <GLES3/gl3.h>
#include <stdint.h>
#include <string.h>
#include <switch.h>
uintptr_t dcr_gl_lookup(const char *);
typedef struct { unsigned char state; GLsync sync; } Fence;
static Fence fences[4096];
static Mutex fence_lock;
static GLsync (*fence_sync)(GLenum, GLbitfield);
static GLenum (*client_wait)(GLsync, GLbitfield, GLuint64);
static void (*delete_sync)(GLsync);
void tasm2_fence_init(void) {
  const char *version = (const char *)glGetString(GL_VERSION);
  if (version && (!strncmp(version, "OpenGL ES 3.", 12))) {
    fence_sync = (void *)dcr_gl_lookup("glFenceSync");
    client_wait = (void *)dcr_gl_lookup("glClientWaitSync");
    delete_sync = (void *)dcr_gl_lookup("glDeleteSync");
  }
  if (!fence_sync || !client_wait || !delete_sync)
    fence_sync = NULL;
  debugPrintf("[gl] NV fences: %s\n", fence_sync ? "GLES3 GPU sync" : "one glFinish per SetFence");
}
static void gen(GLsizei n, GLuint *out) {
  mutexLock(&fence_lock);
  for (int k = 0; k < n; k++) {
    out[k] = 0;
    for (int i = 1; i < 4096; i++)
      if (!fences[i].state) { fences[i].state = 1; out[k] = (GLuint)i; break; }
  }
  mutexUnlock(&fence_lock);
}
static void del(GLsizei n, const GLuint *ids) {
  mutexLock(&fence_lock);
  for (int k = 0; k < n; k++)
    if (ids[k] && ids[k] < 4096) {
      Fence *f = &fences[ids[k]];
      if (f->sync) delete_sync(f->sync);
      memset(f, 0, sizeof *f);
    }
  mutexUnlock(&fence_lock);
}
static void set(GLuint id, GLenum condition) {
  if (condition != 0x84f2u) return; /* GL_ALL_COMPLETED_NV */
  mutexLock(&fence_lock);
  if (id && id < 4096 && fences[id].state) {
    Fence *f = &fences[id];
    if (f->sync) delete_sync(f->sync);
    f->sync = fence_sync ? fence_sync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0) : NULL;
    if (f->sync) {
      f->state = 3;
      glFlush(); /* loader contexts' work must reach the GPU before polling */
    } else {
      glFinish();
      f->state = 2;
    }
  }
  mutexUnlock(&fence_lock);
}
static GLboolean test(GLuint id) {
  GLboolean ready = GL_FALSE;
  mutexLock(&fence_lock);
  if (id && id < 4096) {
    Fence *f = &fences[id];
    if (f->state == 3) {
      GLenum r = client_wait(f->sync, 0, 0);
      if (r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED) f->state = 2;
    }
    ready = f->state == 2;
  }
  mutexUnlock(&fence_lock);
  return ready;
}
static void finish(GLuint id) {
  mutexLock(&fence_lock);
  if (id && id < 4096 && fences[id].state == 3) {
    Fence *f = &fences[id];
    GLenum r;
    do { r = client_wait(f->sync, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull); }
    while (r == GL_TIMEOUT_EXPIRED);
    if (r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED) f->state = 2;
    else fatal_error("GPU fence wait failed for %u (0x%x); see debug.log.", id, r);
  }
  mutexUnlock(&fence_lock);
}
uintptr_t tasm2_fence_wrap(const char *name) {
  if (!strcmp(name, "glGenFencesNV")) return (uintptr_t)gen;
  if (!strcmp(name, "glDeleteFencesNV")) return (uintptr_t)del;
  if (!strcmp(name, "glSetFenceNV")) return (uintptr_t)set;
  if (!strcmp(name, "glTestFenceNV")) return (uintptr_t)test;
  if (!strcmp(name, "glFinishFenceNV")) return (uintptr_t)finish;
  return 0;
}
