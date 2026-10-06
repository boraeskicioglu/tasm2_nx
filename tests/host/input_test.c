#include "switch.h"
#include "tasm2.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void *g_jni_env;
int tasm2_in_action, tasm2_hid_listening = 1, tasm2_swap_ab;
float tasm2_deadzone = .18f;
static u64 held, tick;
static int present = 1, n, connections, back;
static float sticks[4];
static HidTouchScreenState screen;
static double events[22];
static struct {
  int action, x, y, id;
} touches[100];
void debugPrintf(const char *f, ...) { (void)f; }
void rt_pad_setup(int a, int b) {
  (void)a;
  (void)b;
}
void padInitializeDefault(PadState *p) { (void)p; }
void padUpdate(PadState *p) { (void)p; }
int padIsConnected(PadState *p) {
  (void)p;
  return present;
}
u64 rt_pad_read(PadState *p, float *s) {
  (void)p;
  memcpy(s, sticks, sizeof sticks);
  return held;
}
void hidInitializeTouchScreen(void) {}
void hidGetTouchScreenStates(HidTouchScreenState *s, int c) {
  (void)c;
  *s = screen;
}
u64 armGetSystemTick(void) { return tick += 16666667; }
u64 armTicksToNs(u64 t) { return t; }
JObj *jni_str(const char *s) {
  (void)s;
  return calloc(1, sizeof(JObj));
}
void jni_release(JObj *o) { free(o); }
void *tasm2_class(const char *s) { return (void *)s; }
static void hid_event(void *e, void *c, int id, double value) {
  (void)e;
  (void)c;
  assert(id > 0 && id < 22);
  events[id] = value;
}
static void connected(void *e, void *c, void *name) {
  (void)e;
  (void)c;
  (void)name;
  connections++;
}
static void disconnected(void *e, void *c) {
  (void)e;
  (void)c;
  connections--;
}
static void touch(void *e, void *c, int action, int x, int y, int id) {
  (void)e;
  (void)c;
  assert(n < 100);
  touches[n].action = action;
  touches[n].x = x;
  touches[n].y = y;
  touches[n++].id = id;
}
static void down(void *e, void *c, int key) {
  (void)e;
  (void)c;
  assert(key == 4);
  back = 1;
}
static void up(void *e, void *c, int key) {
  (void)e;
  (void)c;
  assert(key == 4);
  back = 0;
}
void *tasm2_native(const char *s) {
  if (strstr(s, "NativeHandleInputEvents"))
    return hid_event;
  if (strstr(s, "NativeControllerConnected"))
    return connected;
  if (strstr(s, "NativeControllerDisconnected"))
    return disconnected;
  if (strstr(s, "touchEvent"))
    return touch;
  if (strstr(s, "OnKeyDown"))
    return down;
  if (strstr(s, "OnKeyUp"))
    return up;
  abort();
}
int main(void) {
  tasm2_input_init();
  held = HidNpadButton_A;
  tasm2_input_poll();
  assert(connections == 1 && events[14] == 1);
  assert(touches[0].action == 1 && touches[0].id == 9);
  n = 0;
  held = 0;
  tasm2_input_poll();
  assert(events[14] == 0);
  assert(touches[0].action == 0 && touches[n - 1].x == -2000 && touches[n - 1].action == 0);
  tasm2_in_action = 1;
  n = 0;
  held = HidNpadButton_B | HidNpadButton_ZR;
  sticks[0] = .6f;
  sticks[1] = .6f;
  tasm2_input_poll();
  assert(n == 0 && events[14] == 1 && events[2] == 1);
  assert(events[3] > .5 && events[4] < -.5);
  screen.count = 1;
  screen.touches[0] = (HidTouchState){42, 123, 456};
  tasm2_input_poll();
  assert(touches[n - 1].action == 1 && touches[n - 1].x == 123);
  screen.touches[0].x = 321;
  tasm2_input_poll();
  assert(touches[n - 1].action == 2 && touches[n - 1].x == 321);
  screen.count = 0;
  tasm2_input_poll();
  assert(touches[n - 2].action == 0 && touches[n - 1].x == -2000);
  tasm2_in_action = 0;
  held = 0;
  tasm2_input_poll();
  held = HidNpadButton_B;
  tasm2_input_poll();
  assert(back);
  tasm2_input_release();
  assert(!back);
  held = 0;
  tasm2_input_poll();
  held = HidNpadButton_A | HidNpadButton_B;
  tasm2_input_poll();
  assert(back);
  tasm2_in_action = 1;
  n = 0;
  tasm2_input_poll();
  assert(!back && n >= 2 && touches[n - 1].action == 0 && touches[n - 1].x == -2000);
  present = 0;
  held = 0;
  tasm2_input_poll();
  assert(connections == 0);
  tasm2_input_shutdown();
  puts("input: exact native press/release, end sentinel, HID mapping, axes and pause release "
       "passed");
  return 0;
}
