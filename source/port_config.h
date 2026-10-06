/* Original TASM2 wrapper configuration. */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H
#define PORT_TITLE "The Amazing Spider-Man 2"
#define PORT_NAME "tasm2_nx"
#define PORT_PACKAGE "com.gameloft.android.ANMP.GloftASHM"
#define PORT_APK_DEFAULT_NAME "The-Amazing-Spider-Man-2.apk"
#define PORT_APK_DESC "The Amazing Spider-Man 2 1.2.8d (armeabi-v7a)"
#define PORT_ABI_DIR "lib/armeabi-v7a/"
#define PORT_SO_REGION_BYTES (32u * 1024u * 1024u)
#define PORT_APK_ROLES                                                                             \
  {                                                                                                \
    .what = "TASM2 1.2.8d", .name = PORT_APK_DEFAULT_NAME, .package = PORT_PACKAGE,                \
    .version_code = 12723, .need = (const char *const[]) {                                         \
      "lib/armeabi-v7a/libtasm2.so", "lib/armeabi-v7a/libgenerator.so", NULL                       \
    }                                                                                              \
  }
#define RT_PACKAGE_MISMATCH_FATAL 1
#define RT_OPENSLES 1
#define RT_PAD_MAX_PLAYERS 1
#define RT_SCREEN_W 1280
#define RT_SCREEN_H 720
#define RT_GL_CHECK 1
#define RT_GL_SWAP_ENDS_FRAME 0 /* port loop owns frame/idle boundaries */
#define RT_BOOST_WATCH_THREAD 1 /* android32 polls during blocking native scene loads */
#define RT_PATH_TRACE_EXTRA "" /* OBB opens are summarized by tasm2_obb_report. */
#define RT_PATH_SD_SUBDIRS "gameloft"
#define RT_PATH_EXTRA_DIRS                                                                         \
  "gameloft", "gameloft/games", "gameloft/games/GloftASHM", "data/shared_prefs", "obb"
#define PORT_LAUNCHER_BYLINE "Bora EskicioÄŸlu / BESK Entertainment"
#define PORT_LAUNCHER_START_NOTE "Bring your own 1.2.8d APK, main OBB and patch OBB."
#endif
