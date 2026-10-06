#ifndef TASM2_POPUPS_H
#define TASM2_POPUPS_H
#include <stddef.h>
#include <stdint.h>
typedef void (*Tasm2PopupCallback)(int, void *);
int tasm2_popups_patch(uint8_t *, size_t, uint32_t);
void tasm2_popups_init(void);
void tasm2_popups_enqueue(Tasm2PopupCallback, void *, int);
void tasm2_popups_tick(void);
#endif
