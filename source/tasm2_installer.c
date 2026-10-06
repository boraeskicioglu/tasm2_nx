/* Installer synchronization state verified in the original TASM2 1.2.8d ELF.
 * Android's nativeAllow creates one-entry counts; nativeStart creates two
 * one-entry worker arrays. The Horizon setup owns installation, so reproduce
 * those four process-lifetime allocations before the engine's first step.
 */
#include "error.h"
#include "util.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void tasm2_installer_init(uint8_t *base, size_t size) {
  static const uint32_t state[] = {0x13b7b30, 0x13b7b34, 0x13b7b38, 0x13b7b3c};
  static const uint32_t got[] = {0x133a8bc, 0x133a8b4, 0x133a8c0, 0x133a8b8};
  static const uint32_t stores[] = {0xe7972002, 0xe5922000, 0xe5823000,
                                    0xe7972001, 0xe5922000, 0xe5823000};
  static const uint32_t offsets[] = {0xffffeecc, 0xffffeed0, 0xffffeef8,
                                     0xffffeefc};
  if (size < 0x13b7b40 || memcmp(base + 0x3fd4ec, stores, sizeof stores) ||
      memcmp(base + 0x3fded0, offsets, sizeof offsets)) {
    fatal_error(
        "Unsupported TASM2 installer layout. Use the original 1.2.8d APK.");
    return;
  }
  unsigned initialized = 0;
  for (unsigned i = 0; i < 4; i++) {
    if (*(uint32_t *)(base + got[i]) !=
        (uint32_t)(uintptr_t)(base + state[i])) {
      fatal_error("TASM2 installer relocation mismatch at slot %u.", i);
      return;
    }
    initialized += *(uint32_t *)(base + state[i]) != 0;
  }
  if (initialized == 4) {
    debugPrintf(
        "[installer] native synchronization state already initialized\n");
    return;
  }
  if (initialized) {
    fatal_error("TASM2 installer synchronization state is incomplete (%u/4).",
                initialized);
    return;
  }
  uint32_t *values[4] = {0};
  for (unsigned i = 0; i < 4; i++) {
    values[i] = malloc(sizeof *values[i]);
    if (!values[i]) {
      for (unsigned j = 0; j < i; j++)
        free(values[j]);
      fatal_error("Could not allocate TASM2 installer synchronization state.");
      return;
    }
    *values[i] = 1;
  }
  /* Publish only after every allocation succeeds; no guest worker exists yet.
   */
  for (unsigned i = 0; i < 4; i++)
    *(uint32_t *)(base + state[i]) = (uint32_t)(uintptr_t)values[i];
  debugPrintf(
      "[installer] checked 1.2.8d native counts=1/1 workers=1/1 initialized\n");
}
