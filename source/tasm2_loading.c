/* One static loading picture before the blocking first native update.
 * Use GLES2 clears rather than a second renderer/thread; restore all GL state
 * touched here. Never reopen the text console after EGL owns the window.
 */
#include <GLES2/gl2.h>
#include <string.h>
static void label(const char *text, int y, int scale) {
  static const char alphabet[] = "LOADINGREFS.M";
  static const unsigned char rows[][7] = {
    {16,16,16,16,16,16,31}, {14,17,17,17,17,17,14},
    {14,17,17,31,17,17,17}, {30,17,17,17,17,17,30},
    {14,4,4,4,4,4,14}, {17,25,25,21,19,19,17},
    {14,17,16,23,17,17,14}, {30,17,17,30,20,18,17},
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {15,16,16,14,1,1,30}, {0,0,0,0,0,12,12}, {17,27,21,21,17,17,17}
  };
  int x = (1280 - (int)strlen(text) * 6 * scale) / 2;
  for (; *text; text++, x += 6 * scale) {
    const char *glyph = strchr(alphabet, *text);
    if (!glyph) continue;
    const unsigned char *bits = rows[glyph - alphabet];
    for (int row = 0; row < 7; row++)
      for (int col = 0; col < 5; col++)
        if (bits[row] & (16 >> col)) {
          glScissor(x + col * scale, y + (6 - row) * scale, scale, scale);
          glClear(GL_COLOR_BUFFER_BIT);
        }
  }
}
void tasm2_loading_draw(void) {
  GLint fb, box[4]; GLfloat clear[4]; GLboolean mask[4];
  GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
  glGetIntegerv(GL_SCISSOR_BOX, box);
  glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
  glGetBooleanv(GL_COLOR_WRITEMASK, mask);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glColorMask(1,1,1,1);
  glDisable(GL_SCISSOR_TEST);
  glClearColor(.025f,.06f,.10f,1);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_SCISSOR_TEST);
  glClearColor(.9f,.94f,1,1);
  label("LOADING...", 365, 7);
  glClearColor(.5f,.68f,.8f,1);
  label("READING GAME FILES", 307, 3);
  glScissor(box[0],box[1],box[2],box[3]);
  if (!scissor) glDisable(GL_SCISSOR_TEST);
  glClearColor(clear[0],clear[1],clear[2],clear[3]);
  glColorMask(mask[0],mask[1],mask[2],mask[3]);
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
}
