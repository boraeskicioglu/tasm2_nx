/* Compile the production native FILE writer wrapper with mocked SD sync. */
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static unsigned flushes,syncs,closes,logs;
static int flush_fail,sync_fail;
void b_fix_errno(void) {}
int host_fsync(int);
#define PORT_PAYLOAD_NAME "tasm2_nx"
#define fsync host_fsync
#define HOST_SAVE_TEST
#include "../../source/tasm2_save.c"
#undef fsync
#include <assert.h>
int host_fsync(int fd) { assert(fd>=0);syncs++;return sync_fail?-1:0; }
void *b_fopen(const char *path,const char *mode) { (void)path;(void)mode;return tmpfile(); }
int b_fclose(void *f) { closes++;return fclose(f); }
int b_fflush(void *f) { flushes++;return flush_fail?-1:fflush(f); }
void *tasm2_obb_open(const char *p,const char *m,int *handled) { (void)p;(void)m;*handled=0;return NULL; }
const char *dcr_game_root(void) { return "/port"; }
const char *dcr_translate_path(const char *p,char *out,size_t n) { (void)out;(void)n;return p; }
void debugPrintf(const char *fmt,...) { (void)fmt;logs++; }
static void close_path(const char *p) { void *f=save_open(p,"ab");assert(f);assert(!save_close(f)); }
int main(void) {
  for(unsigned i=0;i<1000;i++)close_path("/port/data/files/gv3/Timer.bin");
  assert(flushes==0 && syncs==0 && logs==0 && closes==1000 && !writers);
  close_path("/port/data/files/ud_InitPos.sav");
  close_path("/port/data/files/gv30/Token.bin");
  close_path("/port/data/files/gv3.sav");
  assert(flushes==3 && syncs==3);
  close_path("/another/port/data/files/gv3/Timer.bin");
  assert(syncs==3);
  void *telemetry=save_open("/port/data/files/gv3/Token.bin","w");
  void *save=save_open("/port/data/files/profile.sav","wb+");
  tasm2_save_flush();assert(flushes==5 && syncs==5);
  assert(!save_close(telemetry));assert(!save_close(save));assert(syncs==6 && !writers);
  flush_fail=1;save=save_open("/port/data/files/profile.sav","w");
  assert(save_close(save)==-1 && syncs==6 && !writers);flush_fail=0;
  sync_fail=1;save=save_open("/port/data/files/profile.sav","w");
  assert(save_close(save)==-1 && syncs==7 && !writers);sync_fail=0;
  for(unsigned i=0;i<100;i++)close_path("/port/data/files/profile.sav");
  assert(logged_writers>16 && logs==18); /* 16 paths plus two errors */
  puts("Save I/O: telemetry closes avoid fsync/log writes; actual saves, similar prefixes, lifecycle flush and failure propagation retained: PASS");
  return 0;
}
