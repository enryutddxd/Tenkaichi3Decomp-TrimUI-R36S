/*
 * R36S: the PS Vita's system calls port/vita makes, on Linux and SDL2.
 *
 *  - threads (pthreads; the Vita's core masks are kept: the RK3326 has four cores, as many as the Vita's three for
 *    applications plus the system's), semaphores, mutexes, the clock;
 *  - files: "ux0:data/bt3/..." is the program's own folder (where the launcher starts it), "app0:" too;
 *  - the controller: SDL's game controller (ArkOS / PortMaster give the R36S's mapping), read by a thread of its own
 *    so that the game's thread never waits for the system;
 *  - the vertical blank: a clock of 59.94 Hz, the PS2's (the game's pace is built on it);
 *  - sound: one SDL audio device fed with the Vita audio port's blocks (port/vita/vita_audio.c mixes them);
 *  - power, the processor's counters, starting other programs: answered without effect.
 */
#define _GNU_SOURCE
#include <SDL2/SDL.h>
#include <errno.h>
#include <signal.h>
#include <ucontext.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "psp2_shim.h"

extern void Vita_Log(const char *fmt, ...);
extern void R36_GlYield(void);
extern void R36_GlBlocking(void); /* (r36s_gl.c: the GL context let go before this thread waits) */

/* ---- the log of the shim: stderr (err.txt) and the game's log ---- */
void R36_LogLine(const char *fmt, ...) {
    char line[2000];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    fputs(line, stderr);
    fflush(stderr);
    Vita_Log("%s", line);
}

/* ---- the clock ----
   The processor's own counter (the ARM generic timer, CNTVCT, 24 MHz on the RK3326), read straight from the program:
   clock_gettime is a system call on the R36S (its 64-bit kernel has no fast path for 32-bit programs), a microsecond
   or more each, and the renderer asks the time thousands of times a picture. Tried once at the start (an instruction
   the kernel may not allow: SIGILL caught) and checked against clock_gettime; else clock_gettime as before. */
#include <setjmp.h>
static struct timespec sT0;
static volatile int sClk; /* 0 not yet, 1 the counter, 2 clock_gettime */
static uint64_t sCnt0;
static uint32_t sCntMul; /* microseconds per tick << 32 */
static uint64_t sNsMul;  /* nanoseconds per tick << 32 */
static sigjmp_buf sCntJb;
static void cnt_ill(int sig) { (void)sig; siglongjmp(sCntJb, 1); }
static inline uint64_t cnt_read(void) {
#ifdef __arm__
    uint64_t v;
    __asm__ volatile("mrrc p15, 1, %Q0, %R0, c14" : "=r"(v));
    return v;
#else
    return 0;
#endif
}
static uint64_t mono_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
static pthread_mutex_t sClkLock = PTHREAD_MUTEX_INITIALIZER;
static void clk_init(void) {
    pthread_mutex_lock(&sClkLock);
    if (sClk == 0) {
        volatile int ok = 0;
        volatile double seen = -1.0; /* (the counter's rate as measured; -1: not readable) */
        const char *e = getenv("R36_CLOCK");
        clock_gettime(CLOCK_MONOTONIC, &sT0);
#ifdef __arm__
        if (e == NULL || atoi(e) != 0) {
            struct sigaction sa, old;
            memset(&sa, 0, sizeof(sa));
            sa.sa_handler = cnt_ill;
            sigaction(SIGILL, &sa, &old);
            if (sigsetjmp(sCntJb, 1) == 0) {
                uint64_t c0, c1, n0, n1;
                struct timespec d = {0, 20000000L};
                c0 = cnt_read();
                n0 = mono_ns();
                nanosleep(&d, NULL);
                c1 = cnt_read();
                n1 = mono_ns();
                seen = 0.0;
                if (c1 > c0 && n1 > n0) {
                    double hz = (double)(c1 - c0) * 1e9 / (double)(n1 - n0);
                    seen = hz;
                    /* (the counter's rate rounded to a whole kHz; 1 to 200 MHz) */
                    if (hz > 1e6 && hz < 2e8) {
                        double khz = (double)(uint64_t)(hz / 1000.0 + 0.5);
                        sCntMul = (uint32_t)(4294967296.0 * 1000.0 / khz);
                        sNsMul = (uint64_t)(4294967296.0 * 1000000.0 / khz);
                        sCnt0 = c0 - (uint64_t)((double)(n0 - ((uint64_t)sT0.tv_sec * 1000000000ull + (uint64_t)sT0.tv_nsec)) * khz / 1e6);
                        ok = 1;
                        R36_LogLine("r36: the clock from the processor's counter, %.0f kHz\n", khz);
                    }
                }
            }
            sigaction(SIGILL, &old, NULL);
        }
#endif
        if (!ok) {
            R36_LogLine(seen < 0.0 ? "r36: the clock from clock_gettime (the processor's counter is not readable here: SIGILL)\n"
                                   : "r36: the clock from clock_gettime (the processor's counter runs at %.0f Hz)\n", seen);
        }
        __sync_synchronize();
        sClk = ok ? 1 : 2;
    }
    pthread_mutex_unlock(&sClkLock);
}
/* ticks * mul >> 32, without losing the top */
static inline uint64_t mul32(uint64_t d, uint64_t mul) {
    return (d >> 32) * mul + (((d & 0xFFFFFFFFull) * (mul & 0xFFFFFFFFull)) >> 32) + (d & 0xFFFFFFFFull) * (mul >> 32);
}
unsigned long long sceKernelGetProcessTimeWide(void) {
    if (__builtin_expect(sClk == 1, 1)) {
        return mul32(cnt_read() - sCnt0, sCntMul);
    }
    if (sClk == 0) {
        clk_init();
        return sceKernelGetProcessTimeWide();
    }
    return (mono_ns() - ((uint64_t)sT0.tv_sec * 1000000000ull + (uint64_t)sT0.tv_nsec)) / 1000ull;
}
/* nanoseconds on the same clock (r36s_gl.c's call times) */
unsigned long long R36_NowNs(void) {
    if (__builtin_expect(sClk == 1, 1)) {
        return mul32(cnt_read() - sCnt0, sNsMul);
    }
    if (sClk == 0) {
        clk_init();
        return R36_NowNs();
    }
    return mono_ns();
}
unsigned int sceKernelGetProcessTimeLow(void) { return (unsigned int)sceKernelGetProcessTimeWide(); }
int sceKernelDelayThread(SceUInt delay) {
    if (delay == 0) {
        R36_GlYield();
    } else {
        R36_GlBlocking();
    }
    if (delay == 0) {
        sched_yield();
    } else {
        struct timespec t = {(time_t)(delay / 1000000u), (long)(delay % 1000000u) * 1000L};
        while (nanosleep(&t, &t) != 0 && errno == EINTR) {
        }
    }
    return 0;
}
int sceKernelDelayThreadCB(SceUInt delay) { return sceKernelDelayThread(delay); }

/* ---- threads ---- */
#define MAX_TH 64
typedef struct Th {
    int used, started, done, joined;
    char name[32];
    SceKernelThreadEntry entry;
    int stack, mask, prio, exitStatus;
    SceSize arglen;
    void *argp;
    pthread_t pt;
} Th;
static Th sTh[MAX_TH];
static pthread_mutex_t sThLock = PTHREAD_MUTEX_INITIALIZER;
static __thread int tId;
static int sPin = -1;

static void pin(pthread_t pt, int mask) {
    cpu_set_t set;
    int c, any = 0;
    if (sPin < 0) {
        const char *e = getenv("R36_PIN");
        sPin = e != NULL && atoi(e) != 0; /* (off unless pin=1: the system places the threads) */
    }
    if (!sPin || mask == 0) {
        return;
    }
    CPU_ZERO(&set);
    for (c = 0; c < 4; c++) {
        if (mask & (0x10000 << c)) {
            CPU_SET(c, &set);
            any = 1;
        }
    }
    if (any) {
        pthread_setaffinity_np(pt, sizeof(set), &set);
    }
}
static void *th_main(void *arg) {
    Th *t = (Th *)arg;
    tId = (int)(t - sTh) + 1;
    pin(pthread_self(), t->mask);
    t->exitStatus = t->entry(t->arglen, t->argp);
    t->done = 1;
    return NULL;
}
SceUID sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int prio, int stackSize, SceUInt attr, int mask, const void *opt) {
    int i;
    (void)attr; (void)opt;
    pthread_mutex_lock(&sThLock);
    for (i = 1; i < MAX_TH; i++) { /* (0: the main thread) */
        if (!sTh[i].used) {
            memset(&sTh[i], 0, sizeof(Th));
            sTh[i].used = 1;
            snprintf(sTh[i].name, sizeof(sTh[i].name), "%s", name != NULL ? name : "thread");
            sTh[i].entry = entry;
            sTh[i].stack = stackSize;
            sTh[i].mask = mask;
            sTh[i].prio = prio;
            pthread_mutex_unlock(&sThLock);
            return i + 1;
        }
    }
    pthread_mutex_unlock(&sThLock);
    return (SceUID)0x80020000;
}
int sceKernelStartThread(SceUID thid, SceSize arglen, void *argp) {
    Th *t;
    pthread_attr_t a;
    size_t stack;
    if (thid < 2 || thid > MAX_TH || !sTh[thid - 1].used) {
        return (int)0x80020198;
    }
    t = &sTh[thid - 1];
    t->arglen = arglen;
    if (argp != NULL && arglen > 0) {
        t->argp = malloc(arglen);
        memcpy(t->argp, argp, arglen);
    }
    stack = (size_t)t->stack < (1u << 20) ? (1u << 20) : (size_t)t->stack;
    pthread_attr_init(&a);
    pthread_attr_setstacksize(&a, stack);
    if (pthread_create(&t->pt, &a, th_main, t) != 0) {
        pthread_attr_destroy(&a);
        return (int)0x80020000;
    }
    pthread_attr_destroy(&a);
    pthread_setname_np(t->pt, t->name);
    t->started = 1;
    return 0;
}
int sceKernelWaitThreadEnd(SceUID thid, int *stat, SceUInt *timeout) {
    (void)timeout;
    if (thid < 2 || thid > MAX_TH || !sTh[thid - 1].started) {
        return -1;
    }
    if (!sTh[thid - 1].joined) {
        R36_GlBlocking();
        pthread_join(sTh[thid - 1].pt, NULL);
        sTh[thid - 1].joined = 1;
    }
    if (stat != NULL) {
        *stat = sTh[thid - 1].exitStatus;
    }
    return 0;
}
static int sNextForeign = 1000;
int sceKernelGetThreadId(void) {
    if (tId == 0) { /* (a thread not made here: the main one, the renderer's pthreads, SDL's): a number of its own */
        tId = __sync_add_and_fetch(&sNextForeign, 1);
    }
    return tId;
}
int sceKernelGetThreadCurrentPriority(void) {
    int id = sceKernelGetThreadId();
    return id >= 2 && id <= MAX_TH ? sTh[id - 1].prio : 160;
}
/* (a thread that has ended: its place given back) */
int sceKernelDeleteThread(SceUID thid) {
    if (thid < 2 || thid > MAX_TH || !sTh[thid - 1].used) {
        return -1;
    }
    if (sTh[thid - 1].started && !sTh[thid - 1].joined) {
        pthread_join(sTh[thid - 1].pt, NULL);
    }
    free(sTh[thid - 1].argp);
    pthread_mutex_lock(&sThLock);
    memset(&sTh[thid - 1], 0, sizeof(Th));
    pthread_mutex_unlock(&sThLock);
    return 0;
}
int sceKernelGetThreadInfo(SceUID thid, SceKernelThreadInfo *info) {
    clockid_t cid;
    struct timespec ts;
    pthread_t pt;
    if (thid == 0) {
        thid = sceKernelGetThreadId();
    }
    if (thid == sceKernelGetThreadId() || thid == 1) {
        pt = pthread_self();
    } else if (thid >= 2 && thid <= MAX_TH && sTh[thid - 1].started && !sTh[thid - 1].done) {
        pt = sTh[thid - 1].pt;
    } else {
        return -1;
    }
    memset(info, 0, sizeof(*info));
    info->size = sizeof(*info);
    if (pthread_getcpuclockid(pt, &cid) == 0 && clock_gettime(cid, &ts) == 0) {
        info->runClocks = (SceUInt64)ts.tv_sec * 1000000ull + (SceUInt64)(ts.tv_nsec / 1000);
    }
    if (thid >= 2 && thid <= MAX_TH) {
        info->currentPriority = sTh[thid - 1].prio;
        info->currentCpuAffinityMask = sTh[thid - 1].mask;
        snprintf(info->name, sizeof(info->name), "%s", sTh[thid - 1].name);
    }
    info->currentCpuId = info->lastExecutedCpuId = pt == pthread_self() ? sched_getcpu() : 0;
    return 0;
}
int sceKernelChangeThreadCpuAffinityMask(SceUID thid, int mask) {
    if (thid == 0 || thid == sceKernelGetThreadId()) {
        pin(pthread_self(), mask);
    } else if (thid >= 2 && thid <= MAX_TH && sTh[thid - 1].started) {
        pin(sTh[thid - 1].pt, mask);
    }
    return 0;
}
int sceKernelChangeThreadPriority(SceUID thid, int priority) { (void)thid; (void)priority; return 0; }
int sceKernelExitProcess(int res) {
    fflush(NULL);
    _exit(res);
}

/* ---- semaphores, mutexes ---- */
typedef struct Sema { int used, count, max; pthread_mutex_t m; pthread_cond_t c; } Sema;
static Sema sSema[32];
SceUID sceKernelCreateSema(const char *name, SceUInt attr, int initVal, int maxVal, void *option) {
    int i;
    (void)name; (void)attr; (void)option;
    pthread_mutex_lock(&sThLock);
    for (i = 0; i < 32; i++) {
        if (!sSema[i].used) {
            sSema[i].used = 1;
            sSema[i].count = initVal;
            sSema[i].max = maxVal;
            pthread_mutex_init(&sSema[i].m, NULL);
            pthread_cond_init(&sSema[i].c, NULL);
            pthread_mutex_unlock(&sThLock);
            return 0x100 + i;
        }
    }
    pthread_mutex_unlock(&sThLock);
    return -1;
}
int sceKernelWaitSema(SceUID id, int signal, SceUInt *timeout) {
    Sema *s;
    int r = 0;
    if (id < 0x100 || id >= 0x100 + 32) {
        return -1;
    }
    s = &sSema[id - 0x100];
    pthread_mutex_lock(&s->m);
    if (s->count < signal) { /* (about to wait: the GL context to whoever wants it) */
        pthread_mutex_unlock(&s->m);
        R36_GlBlocking();
        pthread_mutex_lock(&s->m);
    }
    if (timeout != NULL) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += (time_t)(*timeout / 1000000u);
        ts.tv_nsec += (long)(*timeout % 1000000u) * 1000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        while (s->count < signal && r == 0) {
            r = pthread_cond_timedwait(&s->c, &s->m, &ts);
        }
    } else {
        while (s->count < signal) {
            pthread_cond_wait(&s->c, &s->m);
        }
    }
    if (s->count >= signal) {
        s->count -= signal;
        r = 0;
    } else {
        r = (int)0x80028005; /* (timed out) */
    }
    pthread_mutex_unlock(&s->m);
    return r;
}
int sceKernelSignalSema(SceUID id, int signal) {
    Sema *s;
    if (id < 0x100 || id >= 0x100 + 32) {
        return -1;
    }
    s = &sSema[id - 0x100];
    pthread_mutex_lock(&s->m);
    s->count += signal;
    if (s->max > 0 && s->count > s->max) {
        s->count = s->max;
    }
    pthread_cond_broadcast(&s->c);
    pthread_mutex_unlock(&s->m);
    return 0;
}
int sceKernelDeleteSema(SceUID id) { if (id >= 0x100 && id < 0x100 + 32) { sSema[id - 0x100].used = 0; } return 0; }
static pthread_mutex_t sMutex[32];
static int sMutexUsed[32];
SceUID sceKernelCreateMutex(const char *name, SceUInt attr, int initCount, void *option) {
    int i;
    (void)name; (void)attr; (void)option;
    pthread_mutex_lock(&sThLock);
    for (i = 0; i < 32; i++) {
        if (!sMutexUsed[i]) {
            pthread_mutexattr_t a;
            sMutexUsed[i] = 1;
            pthread_mutexattr_init(&a);
            pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
            pthread_mutex_init(&sMutex[i], &a);
            pthread_mutexattr_destroy(&a);
            pthread_mutex_unlock(&sThLock);
            if (initCount > 0) {
                pthread_mutex_lock(&sMutex[i]);
            }
            return 0x200 + i;
        }
    }
    pthread_mutex_unlock(&sThLock);
    return -1;
}
int sceKernelLockMutex(SceUID id, int lockCount, unsigned int *timeout) {
    (void)timeout;
    if (id < 0x200 || id >= 0x200 + 32) {
        return -1;
    }
    while (lockCount-- > 0) {
        if (pthread_mutex_trylock(&sMutex[id - 0x200]) != 0) {
            R36_GlBlocking();
            pthread_mutex_lock(&sMutex[id - 0x200]);
        }
    }
    return 0;
}
int sceKernelUnlockMutex(SceUID id, int unlockCount) {
    if (id < 0x200 || id >= 0x200 + 32) {
        return -1;
    }
    while (unlockCount-- > 0) {
        pthread_mutex_unlock(&sMutex[id - 0x200]);
    }
    return 0;
}
int sceKernelDeleteMutex(SceUID id) { (void)id; return 0; }
int sceKernelPowerTick(int type) { (void)type; return 0; }

/* ---- memory ---- */
static void *sBlock[32];
SceUID sceKernelAllocMemBlock(const char *name, int type, SceSize size, void *opt) {
    int i;
    (void)name; (void)type; (void)opt;
    for (i = 0; i < 32; i++) {
        if (sBlock[i] == NULL) {
            void *p = NULL;
            if (posix_memalign(&p, 4096, size) != 0) {
                return (SceUID)0x80020000;
            }
            sBlock[i] = p;
            return 0x300 + i;
        }
    }
    return (SceUID)0x80020000;
}
int sceKernelGetMemBlockBase(SceUID uid, void **base) {
    if (uid < 0x300 || uid >= 0x300 + 32 || sBlock[uid - 0x300] == NULL) {
        return -1;
    }
    *base = sBlock[uid - 0x300];
    return 0;
}
int sceKernelFreeMemBlock(SceUID uid) {
    if (uid >= 0x300 && uid < 0x300 + 32) {
        free(sBlock[uid - 0x300]);
        sBlock[uid - 0x300] = NULL;
    }
    return 0;
}
SceUID sceKernelLoadStartModule(const char *path, SceSize args, void *argp, int flags, void *option, int *status) {
    (void)path; (void)args; (void)argp; (void)flags; (void)option;
    if (status != NULL) {
        *status = 0;
    }
    return (SceUID)0x8002D01C; /* (no such module here) */
}
int sceAppMgrLoadExec(const char *appPath, char *const argv[], void *optParam) { (void)appPath; (void)argv; (void)optParam; return -1; }

/* ---- files ---- */
const char *R36_Path(const char *path, char *buf, size_t size) {
    static const char *home;
    if (home == NULL) {
        home = getenv("BT3_R36_HOME");
        if (home == NULL || home[0] == 0) {
            home = ".";
        }
    }
    if (strncmp(path, "ux0:data/bt3", 12) == 0) {
        snprintf(buf, size, "%s%s", home, path + 12);
    } else if (strncmp(path, "app0:", 5) == 0) {
        snprintf(buf, size, "%s/%s", home, path + 5);
    } else if (strncmp(path, "ux0:", 4) == 0) {
        snprintf(buf, size, "%s/%s", home, path + 4);
    } else {
        snprintf(buf, size, "%s", path);
    }
    return buf;
}
static int err_of(void) { return (int)(0x80010000u | (unsigned)(errno & 0xFFFF)); }
SceUID sceIoOpen(const char *file, int flags, SceMode mode) {
    char p[600];
    int f = 0, fd;
    if ((flags & SCE_O_RDWR) == SCE_O_RDWR) { f = O_RDWR; }
    else if (flags & SCE_O_WRONLY) { f = O_WRONLY; }
    else { f = O_RDONLY; }
    if (flags & SCE_O_APPEND) { f |= O_APPEND; }
    if (flags & SCE_O_CREAT) { f |= O_CREAT; }
    if (flags & SCE_O_TRUNC) { f |= O_TRUNC; }
    if (flags & SCE_O_EXCL) { f |= O_EXCL; }
    fd = open(R36_Path(file, p, sizeof(p)), f | O_CLOEXEC, mode ? mode : 0666);
    return fd < 0 ? err_of() : fd;
}
int sceIoClose(SceUID fd) { return close(fd) < 0 ? err_of() : 0; }
int sceIoRead(SceUID fd, void *data, SceSize size) {
    ssize_t n = read(fd, data, size);
    return n < 0 ? err_of() : (int)n;
}
int sceIoWrite(SceUID fd, const void *data, SceSize size) {
    ssize_t n = write(fd, data, size);
    return n < 0 ? err_of() : (int)n;
}
int sceIoPread(SceUID fd, void *data, SceSize size, SceOff offset) {
    size_t got = 0;
    while (got < size) { /* (a read of a large file may come back in parts) */
        ssize_t n = pread64(fd, (char *)data + got, size - got, (off64_t)offset + (off64_t)got);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return got ? (int)got : err_of();
        }
        if (n == 0) {
            break;
        }
        got += (size_t)n;
    }
    return (int)got;
}
int sceIoPwrite(SceUID fd, const void *data, SceSize size, SceOff offset) {
    ssize_t n = pwrite64(fd, data, size, (off64_t)offset);
    return n < 0 ? err_of() : (int)n;
}
SceOff sceIoLseek(SceUID fd, SceOff offset, int whence) {
    off64_t r = lseek64(fd, (off64_t)offset, whence == SCE_SEEK_END ? SEEK_END : whence == SCE_SEEK_CUR ? SEEK_CUR : SEEK_SET);
    return r < 0 ? (SceOff)err_of() : (SceOff)r;
}
int sceIoRemove(const char *file) { char p[600]; return unlink(R36_Path(file, p, sizeof(p))) < 0 ? err_of() : 0; }
int sceIoMkdir(const char *dir, SceMode mode) { char p[600]; return mkdir(R36_Path(dir, p, sizeof(p)), mode ? mode : 0777) < 0 ? err_of() : 0; }
int sceIoRmdir(const char *dir) { char p[600]; return rmdir(R36_Path(dir, p, sizeof(p))) < 0 ? err_of() : 0; }
int sceIoRename(const char *a, const char *b) {
    char p[600], q[600];
    return rename(R36_Path(a, p, sizeof(p)), R36_Path(b, q, sizeof(q))) < 0 ? err_of() : 0;
}

static SDL_AudioDeviceID sDev; /* (the sound device, below) */

/* ---- the controller ---- */
static volatile unsigned sPadButtons;
static volatile unsigned char sPadAxes[4] = {128, 128, 128, 128}; /* lx, ly, rx, ry */
static volatile unsigned sPadL2R2L3R3; /* bits 0..3: L2, R2, L3, R3 (the R36S has them; the Vita has not) */
static pthread_t sPadThread;
static int sPadUp;
static unsigned char axis8(Sint16 v) {
    int x = ((int)v + 32768) >> 8;
    return (unsigned char)(x < 0 ? 0 : x > 255 ? 255 : x);
}
static void *pad_thread(void *arg) {
    SDL_GameController *gc = NULL;
    SDL_Joystick *js = NULL;
    unsigned lastLog = 0;
    (void)arg;
    for (;;) {
        unsigned b = 0, x = 0;
        SDL_GameControllerUpdate();
        if (gc != NULL && !SDL_GameControllerGetAttached(gc)) {
            SDL_GameControllerClose(gc);
            gc = NULL;
        }
        if (js != NULL && !SDL_JoystickGetAttached(js)) {
            SDL_JoystickClose(js);
            js = NULL;
        }
        if (gc == NULL && js == NULL) {
            int i, n = SDL_NumJoysticks();
            for (i = 0; i < n && gc == NULL; i++) {
                if (SDL_IsGameController(i)) {
                    gc = SDL_GameControllerOpen(i);
                    if (gc != NULL) {
                        R36_LogLine("r36: controller %d: %s (game controller mapping)\n", i, SDL_GameControllerName(gc));
                    }
                }
            }
            if (gc == NULL && n > 0) {
                js = SDL_JoystickOpen(0);
                if (js != NULL) {
                    R36_LogLine("r36: controller 0: %s, without a game controller mapping (%d buttons, %d axes): buttons by number\n",
                                SDL_JoystickName(js), SDL_JoystickNumButtons(js), SDL_JoystickNumAxes(js));
                }
            }
        }
        if (gc != NULL) {
#define GB(btn) SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_##btn)
            if (GB(A)) { b |= SCE_CTRL_CROSS; }         /* (by position: the bottom button is the PS2's cross) */
            if (GB(B)) { b |= SCE_CTRL_CIRCLE; }        /* right */
            if (GB(X)) { b |= SCE_CTRL_SQUARE; }        /* left */
            if (GB(Y)) { b |= SCE_CTRL_TRIANGLE; }      /* top */
            if (GB(BACK)) { b |= SCE_CTRL_SELECT; }
            if (GB(START)) { b |= SCE_CTRL_START; }
            if (GB(DPAD_UP)) { b |= SCE_CTRL_UP; }
            if (GB(DPAD_DOWN)) { b |= SCE_CTRL_DOWN; }
            if (GB(DPAD_LEFT)) { b |= SCE_CTRL_LEFT; }
            if (GB(DPAD_RIGHT)) { b |= SCE_CTRL_RIGHT; }
            if (GB(LEFTSHOULDER)) { b |= SCE_CTRL_LTRIGGER; }
            if (GB(RIGHTSHOULDER)) { b |= SCE_CTRL_RTRIGGER; }
            if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 12000) { x |= 1; }
            if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 12000) { x |= 2; }
            if (GB(LEFTSTICK)) { x |= 4; }
            if (GB(RIGHTSTICK)) { x |= 8; }
            if (GB(GUIDE)) { x |= 16; }
#undef GB
            sPadAxes[0] = axis8(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX));
            sPadAxes[1] = axis8(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY));
            sPadAxes[2] = axis8(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX));
            sPadAxes[3] = axis8(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY));
        } else if (js != NULL) {
            /* no mapping: the R36S's own numbering under ArkOS (its gamepad driver: 0 A/right? ...) as a guess,
               logged when pressed so that a mapping can be made (R36_PADLOG=1) */
            int i, nb = SDL_JoystickNumButtons(js), nh = SDL_JoystickNumHats(js);
            static const unsigned kMap[16] = {SCE_CTRL_CIRCLE, SCE_CTRL_CROSS, SCE_CTRL_TRIANGLE, SCE_CTRL_SQUARE, SCE_CTRL_LTRIGGER, SCE_CTRL_RTRIGGER,
                                              0x10000000u, 0x20000000u, SCE_CTRL_UP, SCE_CTRL_DOWN, SCE_CTRL_LEFT, SCE_CTRL_RIGHT, SCE_CTRL_SELECT,
                                              SCE_CTRL_START, 0x40000000u, 0x80000000u};
            unsigned raw = 0;
            for (i = 0; i < nb && i < 16; i++) {
                if (SDL_JoystickGetButton(js, i)) {
                    raw |= 1u << i;
                    b |= kMap[i];
                }
            }
            if (b & 0x10000000u) { x |= 1; }
            if (b & 0x20000000u) { x |= 2; }
            if (b & 0x40000000u) { x |= 4; }
            if (b & 0x80000000u) { x |= 8; }
            b &= 0x0FFFFFFFu;
            if (nh > 0) {
                Uint8 h = SDL_JoystickGetHat(js, 0);
                if (h & SDL_HAT_UP) { b |= SCE_CTRL_UP; }
                if (h & SDL_HAT_DOWN) { b |= SCE_CTRL_DOWN; }
                if (h & SDL_HAT_LEFT) { b |= SCE_CTRL_LEFT; }
                if (h & SDL_HAT_RIGHT) { b |= SCE_CTRL_RIGHT; }
            }
            if (SDL_JoystickNumAxes(js) >= 2) {
                sPadAxes[0] = axis8(SDL_JoystickGetAxis(js, 0));
                sPadAxes[1] = axis8(SDL_JoystickGetAxis(js, 1));
            }
            if (SDL_JoystickNumAxes(js) >= 4) {
                sPadAxes[2] = axis8(SDL_JoystickGetAxis(js, 2));
                sPadAxes[3] = axis8(SDL_JoystickGetAxis(js, 3));
            }
            if (raw != lastLog && getenv("R36_PADLOG") != NULL) {
                R36_LogLine("r36: controller buttons pressed (by number): %08x\n", raw);
            }
            lastLog = raw;
        }
        sPadButtons = b;
        sPadL2R2L3R3 = x;
        SDL_Delay(4);
    }
    return NULL;
}
void R36_InputInit(void) {
    if (sPadUp) {
        return;
    }
    sPadUp = 1;
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        R36_LogLine("r36: SDL controller: %s\n", SDL_GetError());
        return;
    }
    {
        const char *db = getenv("SDL_GAMECONTROLLERCONFIG_FILE");
        if (db != NULL && SDL_GameControllerAddMappingsFromFile(db) > 0) {
            R36_LogLine("r36: controller mappings from %s\n", db);
        }
        if (access("gamecontrollerdb.txt", R_OK) == 0) {
            SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");
        }
    }
    SDL_JoystickEventState(SDL_IGNORE);
    SDL_GameControllerEventState(SDL_IGNORE);
    pthread_create(&sPadThread, NULL, pad_thread, NULL);
    pthread_setname_np(sPadThread, "bt3_pad");
}
/* L2, R2, L3, R3 and the guide button as bits 0..4 (port/vita/vita_gfx.c Port_PadRead on the R36S) */
unsigned R36_PadExtra(void) { return sPadL2R2L3R3; }
int sceCtrlSetSamplingMode(int mode) { (void)mode; R36_InputInit(); return 0; }
int sceCtrlPeekBufferPositive(int port, SceCtrlData *pad, int count) {
    (void)count;
    if (port != 0 && port != 1) {
        return -1;
    }
    memset(pad, 0, sizeof(*pad));
    pad->timeStamp = sceKernelGetProcessTimeWide();
    pad->buttons = sPadButtons;
    pad->lx = sPadAxes[0];
    pad->ly = sPadAxes[1];
    pad->rx = sPadAxes[2];
    pad->ry = sPadAxes[3];
    {   /* (the sound's library is started by then too: the handlers once more, the first time) */
        static int once;
        if (!once && sDev != 0) {
            extern void R36_CrashHandlers(void);
            once = 1;
            R36_CrashHandlers();
        }
    }
    return 1;
}
int sceCtrlReadBufferPositive(int port, SceCtrlData *pad, int count) { return sceCtrlPeekBufferPositive(port, pad, count); }

/* ---- touch: none ---- */
int sceTouchSetSamplingState(SceUInt32 port, SceTouchSamplingState state) { (void)port; (void)state; return 0; }
int sceTouchGetPanelInfo(SceUInt32 port, SceTouchPanelInfo *info) { (void)port; memset(info, 0, sizeof(*info)); return (int)0x80350000; }
int sceTouchPeek(SceUInt32 port, SceTouchData *data, SceUInt32 count) { (void)port; (void)count; memset(data, 0, sizeof(*data)); return 0; }
int sceTouchRead(SceUInt32 port, SceTouchData *data, SceUInt32 count) { return sceTouchPeek(port, data, count); }

/* ---- the vertical blank: 59.94 per second, on the clock ---- */
#define VB_NUM 1001ull       /* one blank = 1001 / 60000 s */
#define VB_DEN 60000ull
int sceDisplayGetVcount(void) {
    return (int)(sceKernelGetProcessTimeWide() * VB_DEN / (VB_NUM * 1000000ull));
}
int sceDisplayWaitVblankStart(void) {
    unsigned long long now = sceKernelGetProcessTimeWide();
    unsigned long long n = now * VB_DEN / (VB_NUM * 1000000ull) + 1;
    unsigned long long at = (n * VB_NUM * 1000000ull + VB_DEN - 1) / VB_DEN;
    if (at > now) {
        sceKernelDelayThread((SceUInt)(at - now));
    }
    return 0;
}
int sceDisplayWaitVblankStartCB(void) { return sceDisplayWaitVblankStart(); }
int sceDisplayWaitVblankStartMulti(unsigned int vcount) {
    while (vcount-- > 0) {
        sceDisplayWaitVblankStart();
    }
    return 0;
}
int sceDisplayGetRefreshRate(float *fps) { *fps = 59.94f; return 0; }

/* ---- power, counters ---- */
int scePowerSetArmClockFrequency(int f) { (void)f; return 0; }
int scePowerSetBusClockFrequency(int f) { (void)f; return 0; }
int scePowerSetGpuClockFrequency(int f) { (void)f; return 0; }
int scePowerSetGpuXbarClockFrequency(int f) { (void)f; return 0; }
int scePowerGetArmClockFrequency(void) { return 444; } /* (the Vita's figures, so that vita_main.c does not try to raise them) */
int scePowerGetBusClockFrequency(void) { return 222; }
int scePowerGetGpuClockFrequency(void) { return 222; }
int scePowerGetGpuXbarClockFrequency(void) { return 166; }
int scePerfArmPmonReset(SceUID thid) { (void)thid; return -1; }
int scePerfArmPmonSelectEvent(SceUID thid, SceUInt32 counter, SceUInt8 eventCode) { (void)thid; (void)counter; (void)eventCode; return -1; }
int scePerfArmPmonStart(SceUID thid) { (void)thid; return -1; }
int scePerfArmPmonStop(SceUID thid) { (void)thid; return -1; }
int scePerfArmPmonGetCounterValue(SceUID thid, SceUInt32 counter, SceUInt32 *value) {
    (void)thid; (void)counter;
    *value = (SceUInt32)sceKernelGetProcessTimeWide(); /* (microseconds for cycles: the log's figures only) */
    return 0;
}

/* ---- sound: the Vita's audio port on an SDL audio device ---- */
static SDL_AudioDeviceID sDev;
static int sLen, sChans;
int sceAudioOutOpenPort(SceAudioOutPortType type, int len, int freq, SceAudioOutMode mode) {
    SDL_AudioSpec want, have;
    (void)type;
    if (sDev != 0) {
        return 1;
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        R36_LogLine("r36: SDL audio: %s\n", SDL_GetError());
        return -1;
    }
    memset(&want, 0, sizeof(want));
    want.freq = freq;
    want.format = AUDIO_S16SYS;
    want.channels = mode == SCE_AUDIO_OUT_MODE_STEREO ? 2 : 1;
    want.samples = 1024;
    sDev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (sDev == 0) {
        R36_LogLine("r36: no sound device: %s\n", SDL_GetError());
        return -1;
    }
    sLen = len;
    sChans = want.channels;
    R36_LogLine("r36: sound: %d Hz, %d channels, blocks of %d samples (device: %d Hz, %d samples)\n", freq, sChans, len, have.freq, have.samples);
    SDL_PauseAudioDevice(sDev, 0);
    return 1;
}
int sceAudioOutReleasePort(int port) {
    (void)port;
    if (sDev != 0) {
        SDL_CloseAudioDevice(sDev);
        sDev = 0;
    }
    return 0;
}
/* (blocks while two blocks wait in the device's queue: the Vita's port takes a block when the one before is played) */
int sceAudioOutOutput(int port, const void *buf) {
    unsigned bytes = (unsigned)sLen * (unsigned)sChans * 2u;
    (void)port;
    if (sDev == 0) {
        sceKernelDelayThread((SceUInt)((unsigned long long)sLen * 1000000ull / 48000ull));
        return 0;
    }
    while (SDL_GetQueuedAudioSize(sDev) > bytes * 4u) {
        SDL_Delay(2);
    }
    if (buf != NULL) {
        SDL_QueueAudio(sDev, buf, bytes);
    }
    return 0;
}
int sceAudioOutSetVolume(int port, int flag, int *vol) { (void)port; (void)flag; (void)vol; return 0; }
int sceAudioOutSetConfig(int port, SceSize len, int freq, SceAudioOutMode mode) { (void)port; (void)freq; (void)mode; sLen = (int)len; return 0; }

/* ---- a crash: where, into crash.txt (and the log), for the program's map (port/build/r36s/bt3.unstripped) ----
   Written piece by piece with write(): the registers first, then the calls found on the thread's own stack (within
   its bounds, recorded when the thread started), then the last lines of the log that were not on the card yet. Every
   thread gets a stack of its own for the handler (a crash from a full stack still reports). */
extern char __executable_start[], etext[];
static __thread uintptr_t tStackLo, tStackHi;
static void stack_note(void) {
    pthread_attr_t a;
    void *lo = NULL;
    size_t size = 0;
    if (pthread_getattr_np(pthread_self(), &a) == 0) {
        pthread_attr_getstack(&a, &lo, &size);
        pthread_attr_destroy(&a);
        tStackLo = (uintptr_t)lo;
        tStackHi = (uintptr_t)lo + size;
    }
}
static void alt_stack(void) {
    stack_t ss;
    ss.ss_sp = malloc(65536);
    ss.ss_size = 65536;
    ss.ss_flags = 0;
    if (ss.ss_sp != NULL) {
        sigaltstack(&ss, NULL);
    }
    stack_note();
}
/* every pthread made (the renderer's, SDL's, ours) starts here first: build.py links pthread_create through this */
typedef struct Tramp { void *(*fn)(void *); void *arg; } Tramp;
static void *tramp(void *p) {
    Tramp t = *(Tramp *)p;
    free(p);
    alt_stack();
    { extern void R36_ProfThread(void); R36_ProfThread(); } /* (r36s_prof.c) */
    return t.fn(t.arg);
}
extern int __real_pthread_create(pthread_t *th, const pthread_attr_t *attr, void *(*fn)(void *), void *arg);
int __wrap_pthread_create(pthread_t *th, const pthread_attr_t *attr, void *(*fn)(void *), void *arg) {
    Tramp *t = (Tramp *)malloc(sizeof(Tramp));
    if (t == NULL) {
        return __real_pthread_create(th, attr, fn, arg);
    }
    t->fn = fn;
    t->arg = arg;
    return __real_pthread_create(th, attr, tramp, t);
}
static void put(int fd, const char *b, int n) {
    if (fd >= 0 && n > 0) {
        ssize_t w = write(fd, b, (size_t)n);
        (void)w;
    }
}
extern void Vita_CrashLog(int fd); /* port/vita/vita_main.c (R36S): the log's lines not yet written */
static volatile int sCrashed;
static void crash(int sig, siginfo_t *si, void *ctx) {
    char b[1600];
    int n = 0, fd, k = 0;
    ucontext_t *uc = (ucontext_t *)ctx;
    const mcontext_t *m = &uc->uc_mcontext;
    if (__sync_lock_test_and_set(&sCrashed, 1)) { /* (a second thread crashing meanwhile: waits to be ended) */
        for (;;) {
            pause();
        }
    }
    fd = open("crash.txt", O_WRONLY | O_CREAT | O_APPEND, 0644);
#ifdef __arm__
    n = snprintf(b, sizeof(b), "r36: CRASH: signal %d (%s), address %p, thread %d\n  pc %08lx lr %08lx sp %08lx fp %08lx\n"
                 "  r0 %08lx r1 %08lx r2 %08lx r3 %08lx r4 %08lx r5 %08lx r6 %08lx r7 %08lx\n  r8 %08lx r9 %08lx r10 %08lx ip %08lx cpsr %08lx\n",
                 sig, sig == SIGSEGV ? "SEGV" : sig == SIGBUS ? "BUS" : sig == SIGILL ? "ILL" : sig == SIGFPE ? "FPE" : sig == SIGABRT ? "ABRT" : "?",
                 si->si_addr, tId, m->arm_pc, m->arm_lr, m->arm_sp, m->arm_fp, m->arm_r0, m->arm_r1, m->arm_r2, m->arm_r3,
                 m->arm_r4, m->arm_r5, m->arm_r6, m->arm_r7, m->arm_r8, m->arm_r9, m->arm_r10, m->arm_ip, m->arm_cpsr);
    put(fd, b, n);
    put(2, b, n);
    n = snprintf(b, sizeof(b), "  stack %08lx..%08lx, calls:", (unsigned long)tStackLo, (unsigned long)tStackHi);
    if (tStackHi != 0 && m->arm_sp >= tStackLo && m->arm_sp < tStackHi) {
        const unsigned long *sp = (const unsigned long *)m->arm_sp, *end = (const unsigned long *)tStackHi;
        for (; sp < end && k < 48 && n < (int)sizeof(b) - 16; sp++) {
            unsigned long w = *sp;
            if (w >= (unsigned long)__executable_start && w < (unsigned long)etext) {
                n += snprintf(b + n, sizeof(b) - (size_t)n, " %08lx", w);
                k++;
            }
        }
    }
    n += snprintf(b + n, sizeof(b) - (size_t)n, "\n");
    put(fd, b, n);
    put(2, b, n);
#else
    (void)m; (void)k;
    n = snprintf(b, sizeof(b), "r36: CRASH: signal %d, address %p\n", sig, si->si_addr);
    put(fd, b, n);
#endif
    {   /* where pc, lr, sp and the address lie: the program's memory map, and the names of library functions */
        static char maps[65536];
        int mf = open("/proc/self/maps", O_RDONLY), mn = 0, r;
        unsigned long want[4];
        const char *what[4] = {"pc", "lr", "sp", "addr"};
#ifdef __arm__
        want[0] = m->arm_pc; want[1] = m->arm_lr; want[2] = m->arm_sp; want[3] = (unsigned long)si->si_addr;
#else
        want[0] = want[1] = want[2] = want[3] = 0;
#endif
        while (mf >= 0 && mn < (int)sizeof(maps) - 1 && (r = (int)read(mf, maps + mn, sizeof(maps) - 1 - (size_t)mn)) > 0) {
            mn += r;
        }
        if (mf >= 0) {
            close(mf);
        }
        maps[mn] = 0;
        for (int w = 0; w < 4; w++) {
            char *l = maps;
            Dl_info di;
            while (l && *l) {
                char *e = strchr(l, '\n');
                unsigned long lo = strtoul(l, NULL, 16), hi = strtoul(strchr(l, '-') + 1, NULL, 16);
                if (want[w] >= lo && want[w] < hi) {
                    n = snprintf(b, sizeof(b), "  %s %08lx in: %.*s (+%lx)\n", what[w], want[w], e ? (int)(e - l) : (int)strlen(l), l, want[w] - lo);
                    put(fd, b, n);
                    break;
                }
                l = e ? e + 1 : NULL;
            }
            memset(&di, 0, sizeof(di));
            if (w < 2 && dladdr((void *)want[w], &di) && di.dli_fname != NULL) {
                n = snprintf(b, sizeof(b), "  %s = %s + %lx (%s, loaded at %p)\n", what[w], di.dli_sname ? di.dli_sname : "?",
                             di.dli_saddr ? want[w] - (unsigned long)di.dli_saddr : 0ul, di.dli_fname, di.dli_fbase);
                put(fd, b, n);
            }
        }
#ifdef __arm__
        {   /* the instruction's bytes */
            const unsigned char *pc = (const unsigned char *)(m->arm_pc & ~1ul);
            n = snprintf(b, sizeof(b), "  code at pc-8:");
            for (int i = -8; i < 8; i++) {
                n += snprintf(b + n, sizeof(b) - (size_t)n, " %02x", pc[i]);
            }
            n += snprintf(b + n, sizeof(b) - (size_t)n, "\n");
            put(fd, b, n);
        }
        {   /* the words around sp that point into any code */
            const unsigned long *sp = (const unsigned long *)m->arm_sp;
            n = snprintf(b, sizeof(b), "  code addresses on the stack from sp:");
            for (int i = 0, k2 = 0; i < 512 && k2 < 32; i++) {
                unsigned long v = sp[i];
                Dl_info d2;
                if ((v >= (unsigned long)__executable_start && v < (unsigned long)etext) || (v > 0xe0000000ul && dladdr((void *)v, &d2) && d2.dli_sname)) {
                    n += snprintf(b + n, sizeof(b) - (size_t)n, " %08lx", v);
                    k2++;
                }
            }
            n += snprintf(b + n, sizeof(b) - (size_t)n, "\n");
            put(fd, b, n);
        }
#endif
    }
    put(fd, "  the log's last lines:\n", 24);
    Vita_CrashLog(fd);
    if (fd >= 0) {
        close(fd);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}
/* asked to stop (the system's hotkey, gptokeyb): at once */
static void term(int sig) {
    (void)sig;
    _exit(0);
}
/* (again after the system's libraries are loaded and started: one of them may have put a handler of its own in
   front, which then hid where a crash was) */
void R36_CrashHandlers(void) {
    extern void R36_ProfStart(void);
    struct sigaction sa, old;
    int sigs[5] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}, i;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    for (i = 0; i < 5; i++) {
        if (sigaction(sigs[i], &sa, &old) == 0 && (old.sa_flags & SA_SIGINFO ? (void *)old.sa_sigaction : (void *)old.sa_handler) != (void *)crash &&
            old.sa_handler != SIG_DFL && old.sa_handler != SIG_IGN) {
            Dl_info di;
            void *h = old.sa_flags & SA_SIGINFO ? (void *)old.sa_sigaction : (void *)old.sa_handler;
            memset(&di, 0, sizeof(di));
            dladdr(h, &di);
            R36_LogLine("r36: signal %d had a handler of %s (%s): ours put back in front\n", sigs[i], di.dli_fname ? di.dli_fname : "?",
                        di.dli_sname ? di.dli_sname : "?");
        }
    }
    R36_ProfStart();
}
__attribute__((constructor)) static void crash_init(void) {
    struct sigaction sa;
    alt_stack();
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    signal(SIGTERM, term);
    signal(SIGINT, term);
    signal(SIGHUP, term);
    { extern void R36_ProfThread(void); R36_ProfThread(); } /* (the main thread) */
}
