/* Native HID IDs and touch signature verified in the 1.2.8d DEX. */
#include "config.h"
#include "rt_pad.h"
#include "tasm2.h"
#include "util.h"
#include <math.h>
#include <string.h>
#include <switch.h>
enum { TOUCH_UP = 0, TOUCH_DOWN = 1, TOUCH_MOVE = 2 };
static PadState pad;
static void (*hid_event)(void *, void *, int, double);
static void (*connected)(void *, void *, void *);
static void (*disconnected)(void *, void *);
static void (*touch)(void *, void *, int, int, int, int);
static void (*key_down)(void *, void *, int), (*key_up)(void *, void *, int);
static double sent[22];
static int announced, cursor_override = -1, cursor_down, back_down;
static u64 previous;
static float cx = 640, cy = 360;
static uint64_t last_tick;
static struct Finger {
  int active;
  u32 id;
  int x, y;
} fingers[9];
static void event(int id, double v) {
  if (sent[id] != v && hid_event) {
    hid_event(g_jni_env, tasm2_class(C_HID), id, v);
    sent[id] = v;
  }
}
static float axis(float x) {
  float v = fabsf(x);
  if (v < tasm2_deadzone)
    return 0;
  return copysignf((v - tasm2_deadzone) / (1 - tasm2_deadzone), x);
}
int tasm2_cursor_visible(void) { return cursor_override < 0 ? !tasm2_in_action : cursor_override; }
void tasm2_input_init(void) {
  rt_pad_setup(1, 1);
  padInitializeDefault(&pad);
  hidInitializeTouchScreen();
  hid_event = tasm2_native("Java_com_gameloft_android_ANMP_GloftASHM_GLUtils_controller_"
                           "NativeBridgeHIDControllers_NativeHandleInputEvents");
  connected = tasm2_native("Java_com_gameloft_android_ANMP_GloftASHM_GLUtils_controller_"
                           "NativeBridgeHIDControllers_NativeControllerConnected");
  disconnected = tasm2_native("Java_com_gameloft_android_ANMP_GloftASHM_GLUtils_controller_"
                              "NativeBridgeHIDControllers_NativeControllerDisconnected");
  touch = tasm2_native("Java_com_gameloft_glf_GL2JNILib_touchEvent");
  key_down = tasm2_native("Java_com_gameloft_glf_GL2JNILib_OnKeyDown");
  key_up = tasm2_native("Java_com_gameloft_glf_GL2JNILib_OnKeyUp");
  debugPrintf("[input] native HID + multi-touch; Minus/R3 toggles cursor; R3 recenters\n");
}
static void touches(void) {
  HidTouchScreenState ts = {0};
  hidGetTouchScreenStates(&ts, 1);
  int seen[9] = {0};
  for (int k = 0; k < ts.count && k < 9; k++) {
    HidTouchState *t = &ts.touches[k];
    int i;
    for (i = 0; i < 9; i++)
      if (fingers[i].active && fingers[i].id == t->finger_id)
        break;
    int fresh = i == 9;
    if (fresh) {
      for (i = 0; i < 9 && fingers[i].active; i++)
        ;
      if (i == 9)
        continue;
      fingers[i].id = t->finger_id;
      fingers[i].active = 1;
    }
    seen[i] = 1;
    int x = t->x, y = t->y;
    if (touch && (fresh || x != fingers[i].x || y != fingers[i].y))
      touch(g_jni_env, tasm2_class(C_GL), fresh ? TOUCH_DOWN : TOUCH_MOVE, x, y, i);
    fingers[i].x = x;
    fingers[i].y = y;
  }
  for (int i = 0; i < 9; i++)
    if (fingers[i].active && !seen[i]) {
      if (touch)
        touch(g_jni_env, tasm2_class(C_GL), TOUCH_UP, fingers[i].x, fingers[i].y, i);
      fingers[i].active = 0;
      int active = cursor_down;
      for (int j = 0; j < 9; j++)
        active |= fingers[j].active;
      if (!active && touch)
        touch(g_jni_env, tasm2_class(C_GL), TOUCH_UP, -2000, -2000, 0);
    }
}
void tasm2_input_release(void) {
  for (int i = 1; i <= 21; i++)
    event(i, 0);
  for (int i = 0; i < 9; i++)
    if (fingers[i].active) {
      if (touch)
        touch(g_jni_env, tasm2_class(C_GL), TOUCH_UP, fingers[i].x, fingers[i].y, i);
      fingers[i].active = 0;
    }
  if (cursor_down && touch)
    touch(g_jni_env, tasm2_class(C_GL), TOUCH_UP, (int)cx, (int)cy, 9);
  if (back_down && key_up)
    key_up(g_jni_env, tasm2_class(C_GL), 4);
  back_down = 0;
  cursor_down = 0;
  if (touch)
    touch(g_jni_env, tasm2_class(C_GL), TOUCH_UP, -2000, -2000, 0);
  previous = 0;
  last_tick = 0;
}
void tasm2_input_poll(void) {
  padUpdate(&pad);
  float stick[4];
  u64 held = rt_pad_read(&pad, stick), down = held & ~previous, up = previous & ~held;
  u64 tick = armGetSystemTick();
  float dt = last_tick ? armTicksToNs(tick - last_tick) / 1e9f : 1 / 60.f;
  last_tick = tick;
  if (dt > 0.05f)
    dt = 0.05f;
  int present = padIsConnected(&pad);
  if (present && tasm2_hid_listening && !announced) {
    JObj *name = jni_str("Nintendo Switch Controller");
    if (connected)
      connected(g_jni_env, tasm2_class(C_HID), name);
    jni_release(name);
    announced = 1;
    memset(sent, 0, sizeof sent);
    debugPrintf("[input] controller connected\n");
  }
  if (announced && (!present || !tasm2_hid_listening)) {
    tasm2_input_release();
    if (disconnected)
      disconnected(g_jni_env, tasm2_class(C_HID));
    announced = 0;
  }
  if ((down & HidNpadButton_StickR) && (held & HidNpadButton_Minus)) {
    cursor_override = !tasm2_cursor_visible();
    debugPrintf("[input] menu cursor %s\n", cursor_override ? "on" : "off");
  } else if (down & HidNpadButton_StickR) {
    cx = 640;
    cy = 360;
  }
  struct Map {
    u64 mask;
    int id;
  } maps[] = {{HidNpadButton_L, 11},
              {HidNpadButton_R, 12},
              {HidNpadButton_X, 13},
              {HidNpadButton_Y, 15},
              {tasm2_swap_ab ? HidNpadButton_A : HidNpadButton_B, 14},
              {tasm2_swap_ab ? HidNpadButton_B : HidNpadButton_A, 16},
              {HidNpadButton_StickL, 20},
              {HidNpadButton_StickR, 21},
              {HidNpadButton_Plus, 17},
              {HidNpadButton_Minus, 18},
              {HidNpadButton_Up, 7},
              {HidNpadButton_Down, 8},
              {HidNpadButton_Left, 9},
              {HidNpadButton_Right, 10}};
  if (announced) {
    for (unsigned i = 0; i < sizeof maps / sizeof maps[0]; i++)
      event(maps[i].id, (!tasm2_in_action && maps[i].id == 14)   ? !!(held & HidNpadButton_A)
                        : (!tasm2_in_action && maps[i].id == 16) ? 0
                                                                 : !!(held & maps[i].mask));
    event(1, (held & HidNpadButton_ZL) ? 1 : 0);
    event(2, (held & HidNpadButton_ZR) ? 1 : 0);
    event(3, axis(stick[0]));
    event(4, -axis(stick[1]));
    event(5, axis(stick[2]));
    event(6, -axis(stick[3]));
  }
  if (tasm2_cursor_visible()) {
    float x = axis(stick[0]), y = -axis(stick[1]);
    if (held & HidNpadButton_Left)
      x = -1;
    if (held & HidNpadButton_Right)
      x = 1;
    if (held & HidNpadButton_Up)
      y = -1;
    if (held & HidNpadButton_Down)
      y = 1;
    cx = fmaxf(0, fminf(1279, cx + x * 650 * dt));
    cy = fmaxf(0, fminf(719, cy + y * 650 * dt));
    if ((down & HidNpadButton_A) && touch) {
      touch(g_jni_env, tasm2_class(C_GL), TOUCH_DOWN, (int)cx, (int)cy, 9);
      cursor_down = 1;
    }
    if (cursor_down && touch)
      touch(g_jni_env, tasm2_class(C_GL), (up & HidNpadButton_A) ? TOUCH_UP : TOUCH_MOVE, (int)cx,
            (int)cy, 9);
    if (up & HidNpadButton_A) {
      cursor_down = 0;
      int active = 0;
      for (int j = 0; j < 9; j++)
        active |= fingers[j].active;
      if (!active && touch)
        touch(g_jni_env, tasm2_class(C_GL), TOUCH_UP, -2000, -2000, 0);
    }
    if ((down & HidNpadButton_B) && key_down) {
      key_down(g_jni_env, tasm2_class(C_GL), 4);
      back_down = 1;
    }
    if ((up & HidNpadButton_B) && key_up) {
      key_up(g_jni_env, tasm2_class(C_GL), 4);
      back_down = 0;
    }
  } else if (cursor_down) {
    if (touch)
      touch(g_jni_env, tasm2_class(C_GL), TOUCH_UP, (int)cx, (int)cy, 9);
    cursor_down = 0;
    int active = 0;
    for (int j = 0; j < 9; j++)
      active |= fingers[j].active;
    if (!active && touch)
      touch(g_jni_env, tasm2_class(C_GL), TOUCH_UP, -2000, -2000, 0);
  }
  if (back_down && !tasm2_cursor_visible()) {
    if (key_up)
      key_up(g_jni_env, tasm2_class(C_GL), 4);
    back_down = 0;
  }
  touches();
  previous = held;
}
void tasm2_input_shutdown(void) {
  tasm2_input_release();
  if (announced && disconnected)
    disconnected(g_jni_env, tasm2_class(C_HID));
  announced = 0;
}

void tasm2_cursor_position(float *x, float *y) {
  *x = cx;
  *y = cy;
}
