/* Each native archive stream has its own seek position. All streams of one
 * immutable OBB share one Horizon FsFile, as android32 does for APK streams.
 * This avoids hundreds of SD/IPC handles as the game streams an open world.
 * Normal files, saves and APKs still use android32's existing stdio shims.
 */
#include "tasm2_obb.h"
#include "util.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#ifndef HOST_OBB_TEST
#include "dcr_path.h"
#endif
#define BLOCK 65536u
#define READ_BUF 16384u
#define CACHE_BLOCKS 256u
#define CACHE_BUCKETS 512u
#ifndef HOST_OBB_TEST
#include <malloc.h>
#include "nx_init.h"
#endif
typedef struct {
  uint64_t at, stamp;
  size_t len;
  unsigned char *data;
  uint16_t hash_next; /* index + 1; zero ends the chain */
  int indexed;
} CacheBlock;
#ifndef DCR_PATH_MAX
#define DCR_PATH_MAX 1024
#endif
const char *tasm2_storage_obb_path(int);
const char *dcr_translate_path(const char *, char *, size_t);
typedef struct {
  Mutex lock;
  FsFile file;
  uint64_t size, clock, hits, read_ns, lookup_probes;
  unsigned slots;
  CacheBlock blocks[CACHE_BLOCKS];
  uint16_t buckets[CACHE_BUCKETS];
  int ready;
  unsigned active, peak, opens, closes, failures;
  uint64_t reads, bytes;
  unsigned char cache[BLOCK];
} Archive;
static Archive archives[3];
typedef struct { Archive *archive; int64_t pos; } Stream;
static void failure(Archive *a, const char *what, unsigned rc) {
  if (a->failures++ < 8)
    debugPrintf("[obb] %s failed: result=0x%x errno=%d active=%u peak=%u\n",
                what, rc, errno, a->active, a->peak);
}
static int open_archive(Archive *a, const char *path) {
  if (a->ready)
    return 1;
  FsFileSystem *fs = NULL;
  char real[FS_MAX_PATH];
  if (fsdevTranslatePath(path, &fs, real) < 0 || !fs) {
    errno = ENOENT;
    failure(a, "path", 0);
    return 0;
  }
  Result rc = fsFsOpenFile(fs, real, FsOpenMode_Read, &a->file);
  if (R_FAILED(rc)) {
    errno = EIO;
    failure(a, "open", rc);
    return 0;
  }
  s64 size = 0;
  rc = fsFileGetSize(&a->file, &size);
  if (R_FAILED(rc) || size <= 0) {
    fsFileClose(&a->file);
    errno = EIO;
    failure(a, "size", rc);
    return 0;
  }
  a->size = (uint64_t)size;
  a->slots = 1;
  a->blocks[0].data = a->cache;
  a->ready = 1;
  debugPrintf("[obb] shared SD handle: %s (%llu bytes)\n", path,
              (unsigned long long)a->size);
  return 1;
}
/* Bounded LRU, following android32's APK cache policy: grow only with 256 MiB
 * spare, then recycle. The embedded first slot works even under allocation
 * failure. Immutable OBBs need no invalidation while native streams are alive.
 */
static int cache_can_grow(void) {
#ifdef HOST_OBB_TEST
  int host_obb_cache_can_grow(void);
  return host_obb_cache_can_grow();
#else
  struct mallinfo mi = mallinfo();
  uint64_t heap = g_nxinit.heap ? g_nxinit.heap : (992ull << 20);
  return (uint64_t)mi.uordblks + BLOCK + (256ull << 20) <= heap;
#endif
}
/* Fixed hash chains resolve hits without scanning all 256 cached blocks.
 * Entries and the LRU stamp remain under the existing archive mutex. A miss
 * still uses the exact bounded LRU policy, including low-memory fallback.
 */
static unsigned cache_bucket(uint64_t at) {
  uint32_t key = (uint32_t)(at >> 16);
  return ((key ^ (key >> 9)) * 2654435761u) >> 23;
}
static void cache_unindex(Archive *a, CacheBlock *slot) {
  if (!slot->indexed) return;
  uint16_t *link = &a->buckets[cache_bucket(slot->at)];
  uint16_t id = (uint16_t)(slot - a->blocks + 1);
  while (*link && *link != id) link = &a->blocks[*link - 1].hash_next;
  if (*link) *link = slot->hash_next;
  slot->hash_next = 0;
  slot->indexed = 0;
}
static CacheBlock *cache_get(Archive *a, uint64_t pos) {
  uint64_t at = pos & ~(uint64_t)(BLOCK - 1);
  unsigned bucket = cache_bucket(at);
  for (uint16_t id = a->buckets[bucket]; id; id = a->blocks[id - 1].hash_next) {
    CacheBlock *b = &a->blocks[id - 1];
    a->lookup_probes++;
    if (b->len && b->at == at) {
      a->hits++;
      b->stamp = ++a->clock;
      return b;
    }
  }
  CacheBlock *slot = &a->blocks[0], *empty = NULL;
  for (unsigned i = 0; i < a->slots; i++) {
    CacheBlock *b = &a->blocks[i];
    if (!b->len) empty = b;
    if (b->stamp < slot->stamp) slot = b;
  }
  if (empty) slot = empty;
  else if (a->slots < CACHE_BLOCKS && cache_can_grow()) {
    unsigned char *data = malloc(BLOCK);
    if (data) {
      slot = &a->blocks[a->slots++];
      slot->data = data;
    }
  }
  uint64_t got = 0, start = armGetSystemTick();
  size_t n = a->size - at < BLOCK ? (size_t)(a->size - at) : BLOCK;
  cache_unindex(a, slot);
  slot->len = 0;
  Result rc = fsFileRead(&a->file, (s64)at, slot->data, n, FsReadOption_None, &got);
  a->read_ns += armTicksToNs(armGetSystemTick() - start);
  a->reads++;
  if (R_FAILED(rc) || got != n) {
    errno = EIO;
    failure(a, "read", rc);
    return NULL;
  }
  slot->at = at; slot->len = n; slot->stamp = ++a->clock;
  slot->hash_next = a->buckets[bucket];
  a->buckets[bucket] = (uint16_t)(slot - a->blocks + 1);
  slot->indexed = 1;
  a->bytes += got;
  return slot;
}
static int stream_read(void *cookie, char *out, _READ_WRITE_BUFSIZE_TYPE length) {
  Stream *s = cookie;
  Archive *a = s->archive;
  if (length <= 0) return 0;
  size_t done = 0, want = (size_t)length;
  mutexLock(&a->lock);
  if ((uint64_t)s->pos >= a->size) {
    mutexUnlock(&a->lock);
    return 0;
  }
  if (want > a->size - (uint64_t)s->pos)
    want = (size_t)(a->size - (uint64_t)s->pos);
  while (done < want) {
    uint64_t pos = (uint64_t)s->pos;
    CacheBlock *block = cache_get(a, pos);
    if (!block) break;
    size_t off = (size_t)(pos - block->at), n = block->len - off;
    if (n > want - done)
      n = want - done;
    memcpy(out + done, block->data + off, n);
    s->pos += (int64_t)n;
    done += n;
  }
  mutexUnlock(&a->lock);
  return done ? (int)done : want ? -1 : 0;
}
static fpos_t stream_seek(void *cookie, fpos_t off, int whence) {
  Stream *s = cookie;
  int64_t base;
  if (whence == SEEK_SET) base = 0;
  else if (whence == SEEK_CUR) base = s->pos;
  else if (whence == SEEK_END) base = (int64_t)s->archive->size;
  else { errno = EINVAL; return (fpos_t)-1; }
  int64_t delta = (int64_t)off;
  if ((delta > 0 && base > INT64_MAX - delta) ||
      (delta < 0 && delta < -base)) {
    errno = EINVAL;
    return (fpos_t)-1;
  }
  s->pos = base + delta;
  return (fpos_t)s->pos;
}
static int stream_close(void *cookie) {
  Stream *s = cookie;
  mutexLock(&s->archive->lock);
  s->archive->active--;
  s->archive->closes++;
  mutexUnlock(&s->archive->lock);
  free(s);
  return 0;
}
void *tasm2_obb_open(const char *path, const char *mode, int *handled) {
  *handled = 0;
  if (!path || !mode || mode[0] != 'r' || strpbrk(mode, "wa+"))
    return NULL;
  char translated[DCR_PATH_MAX];
  const char *real = dcr_translate_path(path, translated, sizeof translated);
  for (int i = 0; i < 3; i++) {
    const char *owned = tasm2_storage_obb_path(i);
    if (!owned || !*owned || strcmp(real, owned))
      continue;
    *handled = 1;
    Archive *a = &archives[i];
    mutexLock(&a->lock);
    int ok = open_archive(a, owned);
    /* Reserve before publishing FILE: shutdown must not close the physical
     * handle while a concurrent stream allocation is still in progress. */
    if (ok && ++a->active > a->peak) a->peak = a->active;
    mutexUnlock(&a->lock);
    if (!ok)
      return NULL;
    Stream *s = calloc(1, sizeof *s);
    FILE *f = NULL;
    if (s) {
      s->archive = a;
      f = funopen(s, stream_read, NULL, stream_seek, stream_close);
    }
    if (!f) {
      free(s);
      errno = ENOMEM;
      mutexLock(&a->lock);
      a->active--;
      failure(a, "stream allocation", 0);
      mutexUnlock(&a->lock);
      return NULL;
    }
    /* Same newlib FILE ABI as b_fopen / android32's APK funopen streams. */
    setvbuf(f, NULL, _IOFBF, READ_BUF);
    mutexLock(&a->lock);
    a->opens++;
    mutexUnlock(&a->lock);
    return f;
  }
  return NULL;
}
void tasm2_obb_report(void) {
  for (int i = 0; i < 3; i++) {
    Archive *a = &archives[i];
    mutexLock(&a->lock);
    if (a->ready)
      debugPrintf("[obb] %d: active=%u peak=%u opens=%u closes=%u SD reads=%llu (%llu KiB) errors=%u; cache=%u KiB hits=%llu SD time=%llu ms hash probes=%llu\n",
                  i, a->active, a->peak, a->opens, a->closes,
                  (unsigned long long)a->reads, (unsigned long long)(a->bytes >> 10), a->failures, a->slots * (BLOCK >> 10),
                  (unsigned long long)a->hits, (unsigned long long)(a->read_ns / 1000000), (unsigned long long)a->lookup_probes);
    mutexUnlock(&a->lock);
  }
}
void tasm2_obb_shutdown(void) {
  /* Called after native shutdown; live guest streams can still be owned by
   * process-lifetime objects, so their shared handle stays valid until exit. */
  for (int i = 0; i < 3; i++) {
    Archive *a = &archives[i];
    mutexLock(&a->lock);
    if (a->ready && !a->active) {
      fsFileClose(&a->file);
      a->ready = 0;
      for (unsigned j = 1; j < a->slots; j++) free(a->blocks[j].data);
      memset(a->blocks, 0, sizeof a->blocks);
      memset(a->buckets, 0, sizeof a->buckets);
      a->slots = 0;
    }
    mutexUnlock(&a->lock);
  }
}
