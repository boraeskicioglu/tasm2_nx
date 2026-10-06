/* 1.2.8d's CRM initialization has an existing synchronous/local path.
 * The menu queues it asynchronously, then waits for event 6503. With Android
 * online task services absent this holds the equipment shop indefinitely.
 * Select the native local path; its existing offline-success handling opens
 * Shop after local initialization. The game-facing catalog is populated from
 * verified OBB data and the user-requested costume inventory is local.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "util.h"
#include <string.h>
int tasm2_shop_patch(uint8_t *image, size_t size) {
  static const uint32_t caller[] = {
    0xe59fc838u,0xe1a00007u,0xe59f2834u,0xe2861004u,0xe58d6004u,
    0xe3a03001u,0xe08fc00cu,0xe08f2002u,0xe58dc000u,0xeb1ebfd7u,
    0xe3500000u,0x0a0000ccu
  };
  static const uint32_t consumer[] = {
    0xe4963004u,0xe2800068u,0xe5ab3064u,0xe1a01006u,0xeb1339dau,
    0xe285100cu,0xe5950008u,0xe891000eu,0xe35a0000u,
    0xe584006cu,0xe5841070u,0xe5842074u,0xe5843078u,0x1a000081u
  };
  static const uint32_t offline[] = {
    0xe59f452cu,0xe3a03002u,0xe08f4004u,0xe5843018u,
    0xebf27bc6u,0xe3500000u,0x03a03001u,0x05c43002u,0xeaffff30u
  };
  static const uint32_t store_gate[] = {
    0xe59f3054u,0xe79f0003u,0xebfff3deu,0xe3500000u,0x08bd8038u
  };
  if (!image || size < 0xc59df8u ||
      memcmp(image+0x4a9df0,caller,sizeof caller) ||
      memcmp(image+0xc59dc0,consumer,sizeof consumer) ||
      memcmp(image+0x4aa154,offline,sizeof offline) ||
      memcmp(image+0x4abfac,store_gate,sizeof store_gate)) return -1;
  /* Each API and global reference below was checked in this owner's 1.2.8d
   * ELF. Reject another layout before changing any instruction. */
  static const struct {size_t at; uint32_t words[4];} guards[] = {
    {0x3986d4,{0xe59fcfa0u,0xe92d4ff0u,0xe24ddf9fu,0xe58d0024u}},
    {0x37d488,{0xe59f308cu,0xe92d4010u,0xe24dd010u,0xe28d400cu}},
    {0x1128de8,{0xe92d4070u,0xe2514000u,0xe1a05000u,0xe1a06002u}},
    {0x220bf8,{0xe92d4ff0u,0xe1a06001u,0xe5d12000u,0xe24dd014u}},
    {0x52318c,{0xe5903008u,0xe280c004u,0xe3530000u,0x0a00000fu}},
    {0x1b888c,{0xe92d41f0u,0xe1a06001u,0xe5905008u,0xe24dd008u}},
    {0x1b8938,{0xe92d4070u,0xe24dd008u,0xe1a04000u,0xe1a05001u}},
    {0x3806b8,{0xe08f3003u,0xe58d3014u,0xe59f3140u,0xe08f3003u}},
    {0x43cd08,{0xe08f4004u,0xe5940000u,0xebfd01dcu,0xe3500000u}},
    {0x10e45b0,{0xe590200cu,0xe3520000u,0x1a000001u,0xea000005u}},
    {0x3756b8,{0xe92d4ff0u,0xe2807030u,0xe5904038u,0xe24dd01cu}},
    {0x3999b8,{0xebfffb45u,0xe59d0030u,0xe3a03005u,0xe5843048u}}
  };
  for (unsigned i=0;i<sizeof guards/sizeof guards[0];i++)
    if (guards[i].at+sizeof guards[i].words>size ||
        memcmp(image+guards[i].at,guards[i].words,sizeof guards[i].words)) return -1;
  uint32_t local = 0xe3a03000u; /* mov r3,#0: original local initialization */
  memcpy(image+0x4a9e04,&local,4);
  return 0;
}

/* Native COW std::string is one pointer on this ARM ABI. Keep its original
 * catalog backing alive across Shop visits; no copyrighted data is embedded.
 * Parser/counter calls run on the native game thread after Step(). */
const char *tasm2_shop_catalog_json(void);
static uintptr_t game;
static uint32_t catalog_string;
static uintptr_t failed_manager;
static unsigned costume_poll;
void tasm2_shop_bind(uintptr_t base) { game=base; }
static uint32_t value_at(uintptr_t at) {
  uint32_t value; memcpy(&value,(void *)at,4); return value;
}
static const char *const costumes[] = {
  "Scarlet_Spider_suit", "Superior_Spider_suit", "Iron_Spider_suit",
  "Symbiote_Spider_suit", "Ultimate_Comic_suit", "Feature_Spider_suit",
  "EndOfEarth_Spider_suit"
};
static void unlock_costumes(void) {
  uintptr_t inventory=value_at(game+0x134fa14);
  if (!inventory) return;
  uintptr_t items[7];
  void (*id)(uint32_t *,const char *)=(void *)(game+0x220bf8);
  uintptr_t (*find)(uintptr_t,const uint32_t *)=(void *)(game+0x52318c);
  int *(*get)(uintptr_t,int *)=(void *)(game+0x1b888c);
  void (*set)(uintptr_t,const int *)=(void *)(game+0x1b8938);
  /* Verify all seven exact numeric keys before granting anything. The native
   * map finder checks equality and returns its inline value (node +0x14).
   * An inventory value's +0x14 word is not a C-string API; dereferencing it
   * crashed saved-profile boots. Identity is already checked by find(). */
  for (unsigned i=0;i<7;i++) {
    uint32_t key=0; id(&key,costumes[i]); items[i]=find(inventory,&key);
    if (!items[i]) return;
  }
  unsigned changed=0;
  for (unsigned i=0;i<7;i++) {
    /* Native total-owned (+4) and available (+0x20) encrypted integers.
     * Preserve upgrades and any positive quantity. Regular game checkpoints
     * serialize these fields; opening Shop never resets the user's profile. */
    int one=1,temp;
    for (unsigned field=0;field<2;field++) {
      uintptr_t property=items[i]+(field?0x20:4);
      if (*get(property,&temp)<1) { set(property,&one); changed++; }
    }
  }
  if (changed) debugPrintf("[shop] offline costumes available: 7; initialized %u inventory fields\n",changed);
}
/* The native Suit view uses descriptor type 2 and its plain availability
 * byte at +0x70 (checked in 3756b8). Keep one row per verified costume name;
 * prices/profile refreshes may otherwise hide a local costume again. */
static unsigned enable_costume_rows(uintptr_t manager) {
  uint32_t keys[7];uintptr_t selected[7]={0};
  void (*id)(uint32_t *,const char *)=(void *)(game+0x220bf8);
  uintptr_t (*next)(uintptr_t)=(void *)(game+0x10e45b0);
  void (*set)(uintptr_t,const int *)=(void *)(game+0x1b8938);
  for(unsigned i=0;i<7;i++)id(&keys[i],costumes[i]);
  unsigned limit=value_at(manager+0x40);
  uintptr_t end=manager+0x30;
  for(unsigned pass=0;pass<2;pass++) {
    uintptr_t node=value_at(manager+0x38);
    for(unsigned n=0;node && node!=end && n<limit;n++,node=next(node)) {
      uintptr_t item=value_at(node+0x14);
      if(!item)continue;
      uint32_t name=value_at(item+0x10);
      for(unsigned i=0;i<7;i++)if(name==keys[i]) {
        if(!pass) {
          if(!selected[i] || (!*((uint8_t *)selected[i]+0x70) && *((uint8_t *)item+0x70)))selected[i]=item;
        } else *((uint8_t *)item+0x70)=(item==selected[i]);
      }
    }
  }
  unsigned count=0;
  for(unsigned i=0;i<7;i++)if(selected[i]) {
    int type=2;set(selected[i]+0x20,&type);count++;
  }
  return count;
}
void tasm2_shop_tick(void) {
  if (!game) return;
  int poll=!costume_poll;
  if (poll) { costume_poll=59; unlock_costumes(); }
  else costume_poll--;
  uintptr_t manager=value_at(game+0x134f728);
  if (!manager || manager==failed_manager || value_at(manager+0x40)<50 ||
      !value_at(manager+0x44)) return;
  int state=(int)value_at(manager+0x48);
  if (poll && state>=5 && state<=7) enable_costume_rows(manager);
  if (!(state>=0 && state<=4) && state!=-3) return;
  /* Billing metadata has a separate store_items parser. Wait for its normal
   * bootstrap before populating the game-facing catalog and enabling Shop. */
  uintptr_t billing=value_at(game+0x1378508);
  if (!billing || !value_at(billing+0x54)) return;
  unlock_costumes();
  if (!catalog_string) {
    const char *json=tasm2_shop_catalog_json();
    if (!json) { failed_manager=manager; return; }
    unsigned char allocator=0;
    void (*construct)(uint32_t *,const char *,void *)=(void *)(game+0x1128de8);
    construct(&catalog_string,json,&allocator);
    if (!catalog_string) { failed_manager=manager; return; }
  }
  void (*parse)(uintptr_t,const uint32_t *)=(void *)(game+0x3986d4);
  unsigned (*count)(uintptr_t,unsigned)=(void *)(game+0x37d488);
  debugPrintf("[shop] parsing verified OBB catalog locally; native state=%d\n",state);
  *((uint8_t *)manager+0x50)=1; /* Same flag as native request completion. */
  parse(manager,&catalog_string);
  unsigned suits=count(manager,1),items=count(manager,2);
  if (!suits || !items) {
    failed_manager=manager;
    debugPrintf("[shop] catalog rejected; suits=%u items=%u; readiness retained\n",suits,items);
    return;
  }
  unsigned rows=enable_costume_rows(manager);
  debugPrintf("[shop] local costume choices: %u/7; existing inventory retained\n",rows);
  uint32_t ready=5;memcpy((void *)(manager+0x48),&ready,4);
  debugPrintf("[shop] offline catalog ready: suits=%u items=%u; native continuation 5\n",suits,items);
}
