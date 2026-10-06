/* Mesa capabilities and verified NV fence compatibility. */
#include "config.h"
#include "gl_layer.h"
#include "tasm2.h"
#include "util.h"
#include <GLES2/gl2.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
void tasm2_fence_init(void);
uintptr_t tasm2_fence_wrap(const char *);
uintptr_t port_gl_wrap(const char *name, uintptr_t real) {
  (void)real;
  return tasm2_fence_wrap(name);
}
void tasm2_cursor_init(void);
void tasm2_gfx_init(void) {
  debugPrintf("[gl] vendor=%s renderer=%s GLES=%s\n", glGetString(GL_VENDOR),
              glGetString(GL_RENDERER), glGetString(GL_VERSION));
  GLint n = 0;
  glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &n);
  GLint *formats = n > 0 && n < 4096 ? malloc(n * sizeof *formats) : NULL;
  if (formats) {
    glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, formats);
    for (int i = 0; i < n; i++)
      debugPrintf("[gl] compressed format 0x%x\n", formats[i]);
    free(formats);
  }
  const char *required[] = {
      "glMapBufferOES",     "glUnmapBufferOES",          "glTexImage3DOES",
      "glTexSubImage3DOES", "glCompressedTexImage3DOES", "glCompressedTexSubImage3DOES"};
  for (unsigned i = 0; i < sizeof required / sizeof required[0]; i++)
    debugPrintf("[gl] %s -> %p\n", required[i], (void *)dcr_gl_lookup(required[i]));
  tasm2_fence_init();
  tasm2_cursor_init();
}