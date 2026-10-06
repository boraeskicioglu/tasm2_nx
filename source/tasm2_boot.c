/* TASM2 1.2.8d lifecycle, with checked pending UserData checkpoint flush. */
#include "config.h"
#include "dcr_boost.h"
#include "dcr_setup.h"
#include "error.h"
#include "gl_layer.h"
#include "imports.h"
#include "rt_applet.h"
#include "rt_window.h"
#include "tasm2.h"
#include "tasm2_obb.h"
#include <malloc.h>
#include "util.h"
#include <EGL/egl.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>
so_module tasm2_game;
static so_module generator;
static EGLDisplay display;
static EGLConfig egl_config;
static EGLSurface surface;
static EGLContext contexts[5];
static int nextra, view_depth = 24, view_stencil = 8, game_ready, paths_ready;
static void (*step)(void *, void *);
EGLDisplay b_eglGetDisplay(void *);
EGLBoolean b_eglInitialize(EGLDisplay, EGLint *, EGLint *);
EGLBoolean b_eglChooseConfig(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);
EGLContext b_eglCreateContext(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
EGLSurface b_eglCreateWindowSurface(EGLDisplay, EGLConfig, void *, const EGLint *);
EGLBoolean b_eglMakeCurrent(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
EGLBoolean b_eglSwapBuffers(EGLDisplay, EGLSurface);
EGLBoolean b_eglSwapInterval(EGLDisplay, EGLint);
void dcr_audio_pause(int), dcr_audio_close(void);
uint32_t dcr_audio_writes(void);
void tasm2_services_poll(void), tasm2_services_init(void);
void tasm2_loading_draw(void);
void tasm2_fixes_prepare(so_module *), tasm2_fixes_tick(void);
void tasm2_installer_init(uint8_t *, size_t);
void tasm2_save_flush(void);
unsigned tasm2_checkpoint_flush(void);
void tasm2_storage_init(const char *), tasm2_storage_close(void);
void *tasm2_native(const char *name) { return (void *)so_try_find_addr_rx(&tasm2_game, name); }
static void *need(const char *name) {
  void *p = tasm2_native(name);
  if (!p)
    fatal_error("Required native export missing: %s", name);
  return p;
}
#define GLN(n) "Java_com_gameloft_glf_GL2JNILib_" n
static void gl_void(const char *name) {
  void (*f)(void *, void *) = need(name);
  debugPrintf("[boot] %s\n", name);
  f(g_jni_env, tasm2_class(C_GL));
}
static void init_class(const char *sym, const char *cls) {
  void (*f)(void *, void *) = need(sym);
  debugPrintf("[jni] initialize %s\n", cls);
  f(g_jni_env, tasm2_class(cls));
  jni_exception_report(cls);
}
static int load_module(so_module *m, const char *name) {
  char path[768];
  rt_root_path(path, sizeof path, name);
  int rc = so_load(m, path, NULL, PORT_SO_REGION_BYTES);
  if (rc < 0)
    fatal_error("Could not load %s (ELF32 ARM required), error %d.", name, rc);
  so_relocate(m);
  int missing = so_resolve(m, dcr_imports, dcr_imports_count, 1);
  if (missing)
    fatal_error("%s has %d unresolved imports. See debug.log.", name, missing);
  so_fix_kuser_helpers(m);
  if (m == &tasm2_game)
    tasm2_fixes_prepare(m);
  so_finalize(m);
  so_flush_caches(m);
  debugPrintf("[elf] %s mapped at %p, %lu bytes; imports resolved\n", name, m->load_virtbase,
              (unsigned long)m->load_size);
  return 0;
}
int port_load(const char *apk) {
  log_console_progress("Loading game files...", 0);
  tasm2_storage_init(apk);
  load_module(&generator, "libgenerator.so");
  log_console_progress("Loading game engine...", 0);
  load_module(&tasm2_game, "libtasm2.so");
  return 0;
}
void tasm2_set_view(int color, int depth, int stencil, int samples, int coverage) {
  view_depth = depth;
  view_stencil = stencil;
  debugPrintf("[egl] game asks color=%d depth=%d stencil=%d samples=%d coverage=%d\n", color, depth,
              stencil, samples, coverage);
}
void tasm2_setup_paths(void) {
  /* InitViewSettings can request these through its Java setupPaths callback. */
  if (paths_ready)
    return;
  debugPrintf("[boot] setting native resource/files/cache paths\n");
  void (*f)(void *, void *, void *, void *, void *) = need(GLN("setPaths"));
  JObj *r = jni_str(A_RESOURCE), *s = jni_str("/data/data/" PORT_PACKAGE "/files"),
       *c = jni_str("/data/data/" PORT_PACKAGE "/cache");
  f(g_jni_env, tasm2_class(C_GL), r, s, c);
  paths_ready = 1;
  jni_release(r);
  jni_release(s);
  jni_release(c);
  debugPrintf("[save] persistent internal files: sd:/switch/tasm2_nx/data/files\n");
}
int tasm2_context(int index) {
  if (!display)
    return 0;
  if (index < 0)
    return b_eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  if (index > nextra)
    return 0;
  return b_eglMakeCurrent(display, index ? EGL_NO_SURFACE : surface,
                          index ? EGL_NO_SURFACE : surface, contexts[index]);
}
static void egl_start(void) {
  dcr_window_set_size(1280, 720);
  display = b_eglGetDisplay(NULL);
  EGLint maj, min;
  if (!b_eglInitialize(display, &maj, &min))
    fatal_error("EGL initialization failed.");
  EGLint attrs[] = {EGL_SURFACE_TYPE,
                    EGL_WINDOW_BIT,
                    EGL_RENDERABLE_TYPE,
                    EGL_OPENGL_ES2_BIT,
                    EGL_RED_SIZE,
                    8,
                    EGL_GREEN_SIZE,
                    8,
                    EGL_BLUE_SIZE,
                    8,
                    EGL_ALPHA_SIZE,
                    8,
                    EGL_DEPTH_SIZE,
                    view_depth,
                    EGL_STENCIL_SIZE,
                    view_stencil,
                    EGL_NONE};
  EGLint num = 0;
  if (!b_eglChooseConfig(display, attrs, &egl_config, 1, &num) || !num) {
    attrs[13] = 16;
    attrs[15] = 0;
    if (!b_eglChooseConfig(display, attrs, &egl_config, 1, &num) || !num)
      fatal_error("No GLES2 window configuration.");
  }
  EGLint ca[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  contexts[0] = b_eglCreateContext(display, egl_config, EGL_NO_CONTEXT, ca);
  surface = b_eglCreateWindowSurface(display, egl_config, NULL, NULL);
  if (!contexts[0] || !surface || !tasm2_context(0))
    fatal_error("Could not create/make current the GLES2 window.");
  b_eglSwapInterval(display, 1);
  int (*getextra)(void *, void *) = need(GLN("getNumExtraContext"));
  int requested = getextra(g_jni_env, tasm2_class(C_GL));
  const char *ext = eglQueryString(display, EGL_EXTENSIONS);
  if (requested > 4)
    requested = 4;
  if (requested < 0)
    requested = 0;
  if (ext && strstr(ext, "EGL_KHR_surfaceless_context"))
    for (int i = 1; i <= requested; i++) {
      contexts[i] = b_eglCreateContext(display, egl_config, contexts[0], ca);
      if (!contexts[i])
        break;
      nextra = i;
    }
  void (*setextra)(void *, void *, int) = need(GLN("setNumExtraContext"));
  setextra(g_jni_env, tasm2_class(C_GL), nextra);
  debugPrintf("[egl] GLES2 1280x720, %d shared loader contexts (asked %d)\n", nextra, requested);
  tasm2_gfx_init();
}
void port_focus_lost(void) {
  dcr_boost_idle();
  if (game_ready) {
    tasm2_input_release();
    tasm2_checkpoint_flush();
    gl_void(GLN("onPause"));
    init_class("Java_com_gameloft_gameoptions_GameOptions_onPauseGame",
               "com/gameloft/gameoptions/GameOptions");
    dcr_audio_pause(1);
  }
  tasm2_save_flush();
  debugPrintf("[save] game paused; persistent SD writes remain in place\n");
}
void port_focus_gained(void) {
  if (game_ready) {
    gl_void(GLN("onResume"));
    init_class("Java_com_gameloft_gameoptions_GameOptions_onResumeGame",
               "com/gameloft/gameoptions/GameOptions");
    dcr_audio_pause(0);
  }
}
void port_run(void) {
  tasm2_jni_prepare();
  so_execute_init_array(&generator);
  so_free_temp(&generator);
  so_execute_init_array(&tasm2_game);
  so_free_temp(&tasm2_game);
  int (*onload)(void *, void *) = need("JNI_OnLoad");
  int v = onload(g_jni_vm, NULL);
  if (v != 0x00010004)
    fatal_error("TASM2 JNI_OnLoad failed (%d).", v);
  debugPrintf("[jni] JNI_OnLoad 0x%x\n", v);
  tasm2_installer_init(tasm2_game.load_virtbase, tasm2_game.load_size);
  init_class("Java_com_gameloft_glf_GL2JNIActivity_nativeInit", C_ACTIVITY);
  init_class("Java_com_gameloft_GLSocialLib_GameAPI_GameAPIAndroidGLSocialLib_nativeInit",
             "com/gameloft/GLSocialLib/GameAPI/GameAPIAndroidGLSocialLib");
  init_class("Java_com_gameloft_GLSocialLib_PlatformAndroid_nativeInit",
             "com/gameloft/GLSocialLib/PlatformAndroid");
  init_class("Java_com_gameloft_GLSocialLib_facebook_FacebookAndroidGLSocialLib_nativeInit",
             "com/gameloft/GLSocialLib/facebook/FacebookAndroidGLSocialLib");
  gl_void(GLN("init"));
  init_class("Java_com_gameloft_android_ANMP_GloftASHM_GLUtils_SUtils_nativeInit", C_SU);
  init_class("Java_com_gameloft_android_ANMP_GloftASHM_GLUtils_Device_nativeInit", C_DEVICE);
  init_class("Java_com_gameloft_android_ANMP_GloftASHM_DataSharing_nativeInit", C_DS);
  init_class("Java_com_gameloft_android_ANMP_GloftASHM_SendInfo_initMethods", C_SI);
  init_class(
      "Java_com_gameloft_android_ANMP_GloftASHM_PushNotification_SimplifiedAndroidUtils_nativeInit",
      C_PUSH);
  init_class("Java_com_gameloft_gameoptions_GameOptions_onResumeGame",
             "com/gameloft/gameoptions/GameOptions");
  gl_void(GLN("initGL"));
  /* The APK config chooser invokes this before surface creation. It constructs
   * the native OS object which setPaths dereferences; init() only caches JNI. */
  gl_void(GLN("InitViewSettings"));
  tasm2_setup_paths();
  egl_start();
  void (*resize)(void *, void *, int, int) = need(GLN("resize"));
  resize(g_jni_env, tasm2_class(C_GL), 1280, 720);
  gl_void(GLN("onResume"));
  /* Present a port loading picture before step() spends tens of seconds reading
   * archives. Direct EGL presentation deliberately does not announce the first
   * GAME picture or end android32 FastLoad; the native swap below still owns it.
   */
  tasm2_loading_draw();
  if (!eglSwapBuffers(display, surface)) fatal_error("Loading picture presentation failed.");
  debugPrintf("[boot] Loading picture presented; native archive initialization begins\n");
  step = need(GLN("step"));
  tasm2_input_init();
  /* Receive HOME focus loss while still running, so queued checkpoint blocks
   * reach SD before Close Software. The unfocused loop pauses gameplay/GL. */
  Result focus_rc = appletSetFocusHandlingMode(AppletFocusHandlingMode_NoSuspend);
  debugPrintf("[save] HOME checkpoint focus mode: 0x%x\n", (unsigned)focus_rc);
  game_ready = 1;
  int first = 1;
  uint64_t last_report = 0, cpu_report_tick = 0;
  unsigned frame = 0, measured = 0, over33 = 0;
  uint64_t report_start = 0, sum_step = 0, sum_swap = 0, longest = 0;

  while (!rt_exit_requested() && appletMainLoop()) {
    rt_applet_poll();
    if (!rt_focused()) {
      dcr_boost_idle();
      report_start = 0; measured = over33 = 0;
      sum_step = sum_swap = longest = 0;
      svcSleepThread(50000000);
      continue;
    }
    dcr_boost_frame_begin();
    uint64_t start = armGetSystemTick();
    tasm2_services_poll();
    if (frame)
      tasm2_input_poll();
    step(g_jni_env, tasm2_class(C_GL));
    if (!frame) tasm2_services_init(); /* consumers now exist after first step */
    uint64_t step_end = armGetSystemTick();
    tasm2_fixes_tick();
    tasm2_cursor_draw();
    dcr_boost_frame_end((uint64_t)frame + 1); /* restore normal GPU clocks before presentation */
    uint64_t swap_start = armGetSystemTick();
    if (!b_eglSwapBuffers(display, surface))
      fatal_error("EGL presentation failed; see debug.log.");
    if (first) {
      first = 0;
      dcr_boost_first_picture();
      dcr_boost_launch_end();
      debugPrintf("[frame] first successful game presentation; FastLoad ended\n");
    }
    uint64_t ns = armTicksToNs(armGetSystemTick() - start);
    frame++;
    if (!report_start) report_start = start;
    measured++;
    sum_step += armTicksToNs(step_end - start);
    sum_swap += armTicksToNs(armGetSystemTick() - swap_start);
    if (ns > longest) longest = ns;
    if (ns > 33333333ull) over33++;
    uint64_t elapsed = armTicksToNs(armGetSystemTick() - report_start);
    if (elapsed >= 10000000000ull) {
      struct mallinfo mi = mallinfo();
      debugPrintf("[perf] %.1f FPS; step %.2f ms; present %.2f ms; max %.1f ms; >33ms %u/%u; heap %u MiB; audio %u buffers\n",
                  measured * 1e9 / elapsed, sum_step / (measured * 1e6),
                  sum_swap / (measured * 1e6), longest / 1e6, over33, measured,
                  (unsigned)(mi.uordblks >> 20), (unsigned)dcr_audio_writes());
      tasm2_obb_report();
      dcr_boost_report();
      if (!cpu_report_tick || armTicksToNs(start - cpu_report_tick) >= 60000000000ull) {
        dcr_boost_cpu_report();
        cpu_report_tick = start;
      }
      report_start = 0; measured = over33 = 0;
      sum_step = sum_swap = longest = 0;
    }
    if (ns > 100000000ull && armTicksToNs(start - last_report) > 5000000000ull) {
      debugPrintf("[frame] slow frame %u: %llu ms\n", frame, (unsigned long long)(ns / 1000000));
      last_report = start;
    }
  }
  dcr_boost_idle();
  dcr_boost_launch_end();
  rt_applet_stop();
  tasm2_checkpoint_flush();
  tasm2_input_shutdown();
  init_class("Java_com_gameloft_gameoptions_GameOptions_onExit",
             "com/gameloft/gameoptions/GameOptions");
  gl_void(GLN("onPause"));
  init_class("Java_com_gameloft_gameoptions_GameOptions_onPauseGame",
             "com/gameloft/gameoptions/GameOptions");
  dcr_audio_pause(1);
  dcr_audio_close(); /* Engine destroy walks process-lifetime objects; process exit owns them. */
  game_ready = 0;
  b_eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  for (int i = 0; i <= nextra; i++)
    if (contexts[i])
      eglDestroyContext(display, contexts[i]);
  eglDestroySurface(display, surface);
  eglTerminate(display);
  tasm2_save_flush();
  tasm2_obb_report();
  tasm2_obb_shutdown();
  tasm2_storage_close();
  debugPrintf("[save] clean shutdown\n");
}