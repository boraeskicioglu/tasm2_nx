/* Production NV fence bridge: poll pending GPU work; wait/reset/delete. */
#include "../../source/tasm2_fence.c"
#include <assert.h>
#include <stdio.h>
#include <setjmp.h>
static int sync_enabled = 1, complete, finishes, flushes, deleted, waits;
static unsigned sequence;
static jmp_buf failed_wait;
static int force_failure;
void fatal_error(const char *fmt, ...) { (void)fmt; assert(force_failure); longjmp(failed_wait, 1); }
const GLubyte *glGetString(GLenum name) { assert(name == GL_VERSION); return (const GLubyte *)(sync_enabled ? "OpenGL ES 3.2 Mesa" : "OpenGL ES 2.0"); }
void glFinish(void) { finishes++; }
void glFlush(void) { flushes++; }
void debugPrintf(const char *fmt, ...) { (void)fmt; }
static GLsync mock_fence(GLenum c, GLbitfield f) { assert(c == GL_SYNC_GPU_COMMANDS_COMPLETE && !f); return (GLsync)(uintptr_t)++sequence; }
static GLenum mock_wait(GLsync s, GLbitfield f, GLuint64 time) {
  assert(s); waits++;
  if (force_failure) return GL_WAIT_FAILED;
  if (time) { assert(f == GL_SYNC_FLUSH_COMMANDS_BIT); complete = 1; }
  else assert(!f);
  return complete ? GL_CONDITION_SATISFIED : GL_TIMEOUT_EXPIRED;
}
static void mock_delete(GLsync s) { assert(s); deleted++; }
uintptr_t dcr_gl_lookup(const char *name) {
  if (!strcmp(name, "glFenceSync")) return (uintptr_t)mock_fence;
  if (!strcmp(name, "glClientWaitSync")) return (uintptr_t)mock_wait;
  if (!strcmp(name, "glDeleteSync")) return (uintptr_t)mock_delete;
  return 0;
}
int main(void) {
  tasm2_fence_init(); GLuint id;
  gen(1, &id); assert(id && !test(id));
  set(id, 0x84f2); assert(!test(id) && !finishes && flushes == 1);
  complete = 1; assert(test(id)); int before = waits;
  assert(test(id) && waits == before); finish(id); assert(waits == before && !finishes);
  complete = 0; set(id, 0x84f2); assert(deleted == 1 && !test(id));
  finish(id); assert(test(id) && !finishes);
  del(1, &id); assert(deleted == 2 && !test(id));
  gen(1, &id); complete = 0; set(id, 0x84f2); force_failure = 1;
  assert(!test(id));
  if (!setjmp(failed_wait)) { finish(id); assert(0 && "Wait failure must not permit unsafe GPU resource reuse"); }
  force_failure = 0; mutexUnlock(&fence_lock); del(1, &id);
  sync_enabled = 0; fence_sync = NULL; tasm2_fence_init();
  gen(1, &id); set(id, 0x84f2); assert(finishes == 1);
  for (int i = 0; i < 1000; i++) { assert(test(id)); finish(id); }
  assert(finishes == 1); set(id, 0x84f2); assert(finishes == 2);
  del(1, &id); assert(!test(id) && !test(0) && !test(4096));
  puts("NV fences: pending/completed GPU sync, blocking finish, replacement/deletion, one-shot GLES2 fallback: PASS");
}
