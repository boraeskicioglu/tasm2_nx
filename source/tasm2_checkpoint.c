/* 1.2.8d UserData persistence. Keep native codecs, files and profile loaders.
 * Android defaults most progress blocks to cloud-only; the Switch offline
 * profile must also use the existing local ud_%s.sav path. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "util.h"
int tasm2_checkpoint_patch(uint8_t *image, size_t size) {
  static const uint32_t ctor[] = {
    0xe5845008u,0xe5c45019u,0xe5c4501au,0xe5c4501bu,
    0xe5c4501cu,0xe5c4501du,0xe5c4501eu,0xeb21aae4u
  };
  static const struct { size_t at; uint32_t words[4]; } guards[] = {
    {0x51e8a0,{0xe59f2074u,0xe3a01001u,0xe92d4038u,0xe1a04000u}},
    {0x547370,{0xe5902010u,0xe7989003u,0xe3520010u,0xe5993000u}},
    {0x54737c,{0xe5993000u,0xe58d3404u,0x0a000077u,0xebf9fc9du}},
    {0x547568,{0xe59f3074u,0xe08f3003u,0xe5933000u,0xe5d32b50u}},
    {0x547580,{0xe5d33b52u,0xe3530000u,0x1affff7eu,0xe5d0401cu}},
    {0x547f20,{0xe92d4ff0u,0xe24ddf45u,0xe59fa600u,0xe1a04000u}},
    {0x5488a8,{0xe5d65b3cu,0xe3550000u,0x1a000074u,0xe5db300cu}},
    {0x5475f0,{0xe92d4ff0u,0xe1a08000u,0xe59f64c8u,0xed2d8b10u}},
    {0x548ca8,{0xe92d4070u,0xe1a06000u,0xe3a05000u,0xea000001u}}
  };
  if (!image || size < 0x134fa0cu ||
      memcmp(image+0x51e8dc,ctor,sizeof ctor)) return -1;
  for (unsigned i=0;i<sizeof guards/sizeof guards[0];i++)
    if (memcmp(image+guards[i].at,guards[i].words,sizeof guards[i].words)) return -1;
  /* Zero the four bytes at 1c..1f (1f is alignment padding before JSON at 20).
   * This frees two slots to set local=1, keeping the other flags and JSON
   * constructor arguments identical. Derived settings flags stay unchanged. */
  static const uint32_t local[] = {0xe584501cu,0xe3a03001u,0xe5c43019u};
  uint32_t nop=0xe1a00000u;
  memcpy(image+0x51e8ec,local,sizeof local);
  /* SaveIndex must be creatable on the first offline run, without setting the
   * unrelated cloud-ready flag or skipping the native serializer. */
  memcpy(image+0x547384,&nop,sizeof nop);
  return 0;
}
static uintptr_t game;
void tasm2_checkpoint_bind(uintptr_t base) { game=base; }
static uint32_t value_at(uintptr_t at) {
  uint32_t value; memcpy(&value,(void *)at,4); return value;
}
unsigned tasm2_checkpoint_flush(void) {
  if (!game) return 0;
  uintptr_t profile=value_at(game+0x134fa08);
  if (!profile) return 0;
  const uint8_t *p=(const uint8_t *)profile;
  /* Same readiness gates as native UserData::Update (54882c). Never serialize
   * a partially loaded profile or force a checkpoint from live mission state.
   * Only complete blocks already queued by the native gameplay serializers. */
  if (!p[0xb00] || p[0xb3c] || p[0xb3d]) return 0;
  void (*publish)(uintptr_t)=(void *)(game+0x547f20);
  unsigned count=0;
  for (unsigned i=0;i<20;i++) {
    uintptr_t object=value_at(profile+0x830+4*i);
    if (!object) continue;
    const uint8_t *o=(const uint8_t *)object;
    unsigned type=value_at(object+0x10);
    if (type>=20 || !o[0x19] || o[0x1c] || !o[0x1d]) continue;
    uintptr_t block=profile+0x8d8+20*type;
    if (!*((const uint8_t *)block+0xc) || !value_at(block+4) || !value_at(block+0x10)) continue;
    publish(object);
    count++;
  }
  if (count) debugPrintf("[save] published %u pending native checkpoint blocks\n",count);
  return count;
}

