#pragma once
#include <stddef.h>
typedef struct {void *p;} JObj;
extern void *g_jni_env;
JObj *jni_str(const char *);
void jni_release(JObj *);
