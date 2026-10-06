/* Menu cursor overlay with GLES2 state preserved. */
#include "tasm2.h"
#include "util.h"
#include <GLES2/gl2.h>
static GLuint program, vbo;
void tasm2_cursor_draw(void) {
  if (!tasm2_cursor_visible() || !program)
    return;
  GLint oldprog, oldbuf, oldfb, viewport[4], enabled, size, type, norm, stride, abuf;
  void *pointer;
  GLboolean depth = glIsEnabled(GL_DEPTH_TEST), stencil = glIsEnabled(GL_STENCIL_TEST),
            scissor = glIsEnabled(GL_SCISSOR_TEST), blend = glIsEnabled(GL_BLEND), mask[4];
  glGetIntegerv(GL_CURRENT_PROGRAM, &oldprog);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldbuf);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldfb);
  glGetIntegerv(GL_VIEWPORT, viewport);
  glGetBooleanv(GL_COLOR_WRITEMASK, mask);
  glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
  glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_SIZE, &size);
  glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_TYPE, &type);
  glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &norm);
  glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &stride);
  glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &abuf);
  glGetVertexAttribPointerv(0, GL_VERTEX_ATTRIB_ARRAY_POINTER, &pointer);
  float cx, cy;
  tasm2_cursor_position(&cx, &cy);
  GLfloat x = cx / 640 - 1, y = 1 - cy / 360;
  GLfloat verts[] = {x - .012f, y - .012f, x + .012f, y + .012f,
                     x - .012f, y + .012f, x + .012f, y - .012f};
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, 1280, 720);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_BLEND);
  glColorMask(1, 1, 1, 1);
  glUseProgram(program);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof verts, verts, GL_STREAM_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glDrawArrays(GL_LINES, 0, 4);
  glBindBuffer(GL_ARRAY_BUFFER, abuf);
  glVertexAttribPointer(0, size, type, norm, stride, pointer);
  if (!enabled)
    glDisableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, oldbuf);
  glUseProgram(oldprog);
  glBindFramebuffer(GL_FRAMEBUFFER, oldfb);
  glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
  glColorMask(mask[0], mask[1], mask[2], mask[3]);
  if (depth)
    glEnable(GL_DEPTH_TEST);
  if (stencil)
    glEnable(GL_STENCIL_TEST);
  if (scissor)
    glEnable(GL_SCISSOR_TEST);
  if (blend)
    glEnable(GL_BLEND);
}
void tasm2_cursor_init(void) {
  const char *vs = "attribute vec2 p;void main(){gl_Position=vec4(p,0.,1.);}";
  const char *fs = "precision mediump float;void main(){gl_FragColor=vec4(1.,0.8,0.,1.);}";
  GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(v, 1, &vs, NULL);
  glCompileShader(v);
  glShaderSource(f, 1, &fs, NULL);
  glCompileShader(f);
  program = glCreateProgram();
  glAttachShader(program, v);
  glAttachShader(program, f);
  glBindAttribLocation(program, 0, "p");
  glLinkProgram(program);
  glDeleteShader(v);
  glDeleteShader(f);
  GLint ok;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    glDeleteProgram(program);
    program = 0;
    debugPrintf("[gl] menu cursor shader failed\n");
  }
  glGenBuffers(1, &vbo);
}