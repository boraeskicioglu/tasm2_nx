#ifndef TASM2_H
#define TASM2_H
#include "jni.h"
#include "so_util.h"
#define C_GL "com/gameloft/glf/GL2JNILib"
#define C_ACTIVITY "com/gameloft/glf/GL2JNIActivity"
#define C_GAME "com/gameloft/android/ANMP/GloftASHM/Game"
#define C_SU "com/gameloft/android/ANMP/GloftASHM/GLUtils/SUtils"
#define C_DEVICE "com/gameloft/android/ANMP/GloftASHM/GLUtils/Device"
#define C_HID "com/gameloft/android/ANMP/GloftASHM/GLUtils/controller/NativeBridgeHIDControllers"
#define C_SI "com/gameloft/android/ANMP/GloftASHM/SendInfo"
#define C_DS "com/gameloft/android/ANMP/GloftASHM/DataSharing"
#define C_PUSH "com/gameloft/android/ANMP/GloftASHM/PushNotification/SimplifiedAndroidUtils"
#define A_RESOURCE "/sdcard/gameloft/games/GloftASHM"
#define A_OBB "/sdcard/Android/obb/" PORT_PACKAGE
extern so_module tasm2_game;
extern int tasm2_in_action, tasm2_hid_listening;
extern char tasm2_device_id[40];
extern float tasm2_deadzone;
extern int tasm2_swap_ab;
void *tasm2_native(const char *name);
void *tasm2_class(const char *name);
void tasm2_setup_paths(void);
void tasm2_input_init(void);
void tasm2_input_poll(void);
void tasm2_input_release(void);
void tasm2_input_shutdown(void);
int tasm2_cursor_visible(void);
void tasm2_cursor_position(float *, float *);
void tasm2_cursor_draw(void);
void tasm2_gfx_init(void);
int tasm2_context(int index);
void tasm2_jni_prepare(void);
#endif
