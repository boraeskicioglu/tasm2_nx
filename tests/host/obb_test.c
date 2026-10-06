/* Actual FILE/funopen production reader: many streams, no per-stream SD fd. */
#include "../../source/tasm2_obb.c"
#include <assert.h>
#include <pthread.h>
static const uint64_t size[2] = {1236510228ull, 98576965ull};
static FsFileSystem fs;
static unsigned physical_opens, physical_closes;
static int fail_read, allow_growth = 1;
int host_obb_cache_can_grow(void) { return allow_growth; }
u64 armGetSystemTick(void) { static u64 tick; return __atomic_add_fetch(&tick,1,__ATOMIC_RELAXED); }
u64 armTicksToNs(u64 tick) { return tick * 1000; }
const char *tasm2_storage_obb_path(int i) { return i == 0 ? "main.obb" : i == 1 ? "patch.obb" : ""; }
const char *dcr_translate_path(const char *p, char *out, size_t cap) {
  (void)out; (void)cap; return p;
}
void debugPrintf(const char *fmt, ...) { (void)fmt; }
int fsdevTranslatePath(const char *p, FsFileSystem **out, char *real) {
  *out = &fs; strcpy(real, p); return 0;
}
Result fsFsOpenFile(FsFileSystem *f, const char *p, unsigned mode, FsFile *out) {
  assert(f == &fs && mode == FsOpenMode_Read);
  physical_opens++; out->index = !strcmp(p, "patch.obb"); return 0;
}
Result fsFileGetSize(FsFile *f, s64 *out) { *out = (s64)size[f->index]; return 0; }
static unsigned char byte_at(int index, uint64_t pos) { return (unsigned char)((pos * 17 + (pos >> 8) + (pos >> 16) * 3 + index * 71) & 255); }
Result fsFileRead(FsFile *f, s64 at, void *out, size_t n, unsigned option, u64 *got) {
  assert(option == FsReadOption_None && at >= 0 && (uint64_t)at + n <= size[f->index]);
  if (fail_read) { *got = 0; return 123; }
  for (size_t i = 0; i < n; i++) ((unsigned char *)out)[i] = byte_at(f->index, (uint64_t)at + i);
  *got = n; return 0;
}
void fsFileClose(FsFile *f) { (void)f; physical_closes++; }
static void read_at(FILE *f, int index, uint64_t pos, size_t length) {
  unsigned char data[8192]; assert(length <= sizeof data);
  assert(!fseeko(f, (off_t)pos, SEEK_SET));
  assert(fread(data, 1, length, f) == length);
  assert((uint64_t)ftello(f) == pos + length);
  for (size_t i=0;i<length;i++) {
    if(data[i]!=byte_at(index,pos+i)) {
      fprintf(stderr,"OBB mismatch: index=%d pos=%llu i=%llu got=%u expected=%u\n",index,(unsigned long long)pos,(unsigned long long)i,data[i],byte_at(index,pos+i));
      abort();
    }
  }
}
static void *worker(void *arg) {
  int index = (int)(intptr_t)arg, handled;
  FILE *f = tasm2_obb_open(index ? "patch.obb" : "main.obb", "rb", &handled);
  assert(f && handled);
  for (int i = 0; i < 150; i++) read_at(f, index, 700001u * (unsigned)i % (size[index] - 8192), 8192);
  assert(!fclose(f)); return NULL;
}
int main(void) {
  FILE *streams[1024]; int handled;
  assert(!tasm2_obb_open("save.bin", "rb", &handled) && !handled);
  assert(!tasm2_obb_open("main.obb", "r+b", &handled) && !handled);
  for (int i = 0; i < 1024; i++) {
    int index = i & 1;
    streams[i] = tasm2_obb_open(index ? "patch.obb" : "main.obb", "rb", &handled);
    assert(streams[i] && handled);
    read_at(streams[i], index, (uint64_t)i * 8111, 4096);
  }
  assert(physical_opens == 2 && archives[0].active == 512 && archives[1].active == 512);
  read_at(streams[0], 0, size[0] - 23, 23);
  assert(fgetc(streams[0]) == EOF && feof(streams[0]));
  assert(!fseeko(streams[0], -23, SEEK_END));
  assert(fgetc(streams[0]) == byte_at(0, size[0] - 23));
  assert(fseeko(streams[0], -1, SEEK_SET) == -1);
  assert(!fseeko(streams[0], (off_t)size[0] + 99, SEEK_SET));
  assert(fgetc(streams[0]) == EOF);
  assert(!fseeko(streams[0], 123, SEEK_SET));
  assert(fgetc(streams[0]) == byte_at(0, 123));
  for (unsigned j = 0; j < archives[0].slots; j++) archives[0].blocks[j].len = 0;
  fail_read = 1;
  assert(!fseeko(streams[0], 1000001, SEEK_SET));
  assert(fgetc(streams[0]) == EOF && ferror(streams[0]));
  clearerr(streams[0]); fail_read = 0;
  read_at(streams[0], 0, 1000001, 4096);
  for (int i = 0; i < 1024; i++) assert(!fclose(streams[i]));
  assert(!archives[0].active && !archives[1].active);
  assert(archives[0].closes == 512 && archives[1].closes == 512);
  for (int i = 0; i < 10000; i++) {
    FILE *f = tasm2_obb_open("main.obb", "rb", &handled); assert(f); assert(!fclose(f));
  }
  /* Alternating seeks across 32 streams used to thrash the single hot block.
   * Compare actual SD requests, and verify every byte over block eviction.
   */
  FILE *trace = tasm2_obb_open("main.obb", "rb", &handled); assert(trace);
  for (unsigned j = 0; j < archives[0].slots; j++) archives[0].blocks[j].len = 0;
  uint64_t before_reads = archives[0].reads;
  for (int round = 0; round < 30; round++)
    for (int j = 0; j < 32; j++) read_at(trace, 0, 50000000ull + BLOCK * j, 64);
  assert(archives[0].reads - before_reads <= 33); /* old cache: 960 requests */
  /* A 192-block working set exceeds the previous 8 MiB budget. */
  for (unsigned j = 0; j < archives[0].slots; j++) archives[0].blocks[j].len = 0;
  before_reads = archives[0].reads;
  for (int round = 0; round < 3; round++)
    for (int j = 0; j < 192; j++) read_at(trace, 0, 100663296ull + BLOCK * j, 64);
  assert(archives[0].reads - before_reads == 192);
  assert(archives[0].slots >= 192 && CACHE_BLOCKS * BLOCK == (16u << 20));
  /* Count hit comparisons on a full cache, rather than assert wall-clock
   * timing on a host. 256 blocks, 100 passes: no additional physical reads.
   */
  for (unsigned j = 0; j < CACHE_BLOCKS; j++)
    read_at(trace, 0, 150994944ull + BLOCK * j, 64);
  before_reads = archives[0].reads;
  uint64_t probes = archives[0].lookup_probes;
  for (unsigned round = 0; round < 100; round++)
    for (unsigned j = 0; j < CACHE_BLOCKS; j++)
      read_at(trace, 0, 150994944ull + BLOCK * j, 64);
  assert(archives[0].reads == before_reads);
  uint64_t hit_probes = archives[0].lookup_probes - probes;
  assert(hit_probes <= 3ull * 100 * CACHE_BLOCKS);
  printf("OBB full-cache hit trace: %llu probes / 25600 hits (old linear scan: 3289600 comparisons)\n", (unsigned long long)hit_probes);
  /* Force hash collisions, evict more blocks than fit, and reread. */
  unsigned collision_bucket = cache_bucket(0), found = 0;
  uint64_t collision_positions[300];
  for (uint64_t block = 0; block < size[0] / BLOCK && found < 300; block++)
    if (cache_bucket(block * BLOCK) == collision_bucket)
      collision_positions[found++] = block * BLOCK;
  assert(found > 1);
  for (int pass = 0; pass < 3; pass++)
    for (unsigned j = 0; j < found; j++) read_at(trace, 0, collision_positions[j] + 7, 8192);
  allow_growth = 0;
  unsigned slots = archives[0].slots;
  for (int j = 0; j < 300; j++) read_at(trace, 0, 80000000ull + BLOCK * j, 8192);
  assert(archives[0].slots == slots && slots <= CACHE_BLOCKS);
  allow_growth = 1;
  assert(!fclose(trace));
  pthread_t workers[4];
  for (int i = 0; i < 4; i++) assert(!pthread_create(&workers[i], NULL, worker, (void *)(intptr_t)(i & 1)));
  for (int i = 0; i < 4; i++) pthread_join(workers[i], NULL);
  assert(physical_opens == 2 && !archives[0].active && !archives[1].active);
  tasm2_obb_shutdown(); assert(physical_closes == 2);
  /* A closed archive can reopen with clean hash chains. */
  trace = tasm2_obb_open("main.obb", "rb", &handled); assert(trace);
  read_at(trace, 0, 0, 8192); read_at(trace, 0, BLOCK + 17, 8192);
  assert(!fclose(trace)); tasm2_obb_shutdown(); assert(physical_closes == 3);
  puts("OBB: 1024 simultaneous independent streams, 10000 close/reopen cycles, threaded reads, EOF/seek/error recovery, bounded LRU/recycling, 960 alternating reads in <=33 SD requests and 576 larger-working-set reads in 192 requests: PASS");
}
