/* Read-only expansion streams, following android32's APK funopen pattern. */
#ifndef TASM2_OBB_H
#define TASM2_OBB_H
#include <stddef.h>
void *tasm2_obb_open(const char *, const char *, int *);
void tasm2_obb_report(void);
void tasm2_obb_shutdown(void);
#endif
