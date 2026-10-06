#pragma once
#include <pthread.h>
#include <stdint.h>
#include <assert.h>
typedef pthread_mutex_t Mutex;
static inline void mutexLock(Mutex *m){
#if defined(__CYGWIN__) || defined(__MSYS__)
 /* Horizon uses zero-initialized tags; MSYS pthread mutexes use a nonzero
  * sentinel. Lazily initialize the host equivalent under a real mutex. */
 static pthread_mutex_t init_lock=PTHREAD_MUTEX_INITIALIZER;
 assert(!pthread_mutex_lock(&init_lock));
 if (!*m) assert(!pthread_mutex_init(m,NULL));
 assert(!pthread_mutex_unlock(&init_lock));
#endif
 assert(!pthread_mutex_lock(m));
}
static inline void mutexUnlock(Mutex *m){assert(!pthread_mutex_unlock(m));}
static inline void mutexInit(Mutex *m){assert(!pthread_mutex_init(m,NULL));}
static inline int fsdevCommitDevice(const char *s){(void)s;return 0;}

typedef uint64_t u64;
typedef uint32_t u32;
typedef struct {int unused;} PadState;
typedef struct {u32 finger_id,x,y;} HidTouchState;
typedef struct {int count;HidTouchState touches[16];} HidTouchScreenState;
#define HidNpadButton_A (1ull<<0)
#define HidNpadButton_B (1ull<<1)
#define HidNpadButton_X (1ull<<2)
#define HidNpadButton_Y (1ull<<3)
#define HidNpadButton_StickL (1ull<<4)
#define HidNpadButton_StickR (1ull<<5)
#define HidNpadButton_L (1ull<<6)
#define HidNpadButton_R (1ull<<7)
#define HidNpadButton_ZL (1ull<<8)
#define HidNpadButton_ZR (1ull<<9)
#define HidNpadButton_Plus (1ull<<10)
#define HidNpadButton_Minus (1ull<<11)
#define HidNpadButton_Left (1ull<<12)
#define HidNpadButton_Up (1ull<<13)
#define HidNpadButton_Right (1ull<<14)
#define HidNpadButton_Down (1ull<<15)
void padInitializeDefault(PadState *);
void padUpdate(PadState *);
int padIsConnected(PadState *);
void hidInitializeTouchScreen(void);
void hidGetTouchScreenStates(HidTouchScreenState *,int);
u64 armGetSystemTick(void);
u64 armTicksToNs(u64);

#ifdef HOST_OPENSLES_TEST
#include <time.h>
typedef pthread_cond_t CondVar;
typedef struct { u32 handle; } Thread;
#define R_FAILED(result) ((result) != 0)
static inline void condvarInit(CondVar *cv){pthread_cond_init(cv,NULL);}
static inline void condvarWakeAll(CondVar *cv){pthread_cond_broadcast(cv);}
static inline void condvarWaitTimeout(CondVar *cv,Mutex *m,int64_t ns){
 struct timespec t;clock_gettime(CLOCK_REALTIME,&t);t.tv_nsec+=ns;
 t.tv_sec+=t.tv_nsec/1000000000;t.tv_nsec%=1000000000;
 pthread_cond_timedwait(cv,m,&t);
}
static inline int threadCreate(Thread *t,void (*fn)(void *),void *arg,void *stack,size_t size,int prio,int core){
 (void)t;(void)fn;(void)arg;(void)stack;(void)size;(void)prio;(void)core;return -1;
}
static inline int threadStart(Thread *t){(void)t;return -1;}
static inline u32 threadGetCurHandle(void){return 1;}
void svcSleepThread(int64_t);
#endif

#ifdef HOST_SPACE_TEST
typedef int64_t s64;
typedef uint32_t Result;
typedef struct {int mounted;} FsFileSystem;
#define R_SUCCEEDED(result) ((result) == 0)
#ifndef R_FAILED
#define R_FAILED(result) ((result) != 0)
#endif
FsFileSystem *fsdevGetDeviceFileSystem(const char *);
Result fsFsGetFreeSpace(FsFileSystem *, const char *, s64 *);
Result fsFsGetTotalSpace(FsFileSystem *, const char *, s64 *);
#endif

#ifdef HOST_OBB_TEST
#include <stddef.h>
typedef int64_t s64;
typedef uint32_t Result;
typedef struct {int mounted;} FsFileSystem;
typedef struct {int index;} FsFile;
#define FS_MAX_PATH 0x301
#define FsOpenMode_Read 1
#define FsReadOption_None 0
#define R_FAILED(result) ((result) != 0)
int fsdevTranslatePath(const char *, FsFileSystem **, char *);
Result fsFsOpenFile(FsFileSystem *, const char *, unsigned, FsFile *);
Result fsFileGetSize(FsFile *, s64 *);
Result fsFileRead(FsFile *, s64, void *, size_t, unsigned, u64 *);
void fsFileClose(FsFile *);
#endif
