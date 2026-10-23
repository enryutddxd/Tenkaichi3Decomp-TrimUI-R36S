/*
 * R36S: where the processor's time goes, written to prof.txt every 10 seconds.
 *
 * Each thread of ours gets a timer on its own processor time (1 ms): a signal then notes where the thread is (its
 * program counter) and who called that (two levels: the frame pointers of this program's code, or, from inside a
 * library such as the graphics driver, the first return addresses into this program found on the stack). The notes
 * are counted per thread in a table only that thread writes. A thread of its own adds them up by function every 10
 * seconds, with the names from bt3.sym (the program's symbols, made by build.py), and writes what changed: per thread,
 * the processor time it took, the functions where it was ("self") and those it was under ("with what they call").
 * Also the processors' and the graphics chip's clocks then. R36_PROF=0 in the environment: off.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

extern char __executable_start[], etext[];
void R36_LogLine(const char *fmt, ...);

#define SLOTS 32
#define ENTRIES 8192 /* (a power of two) */
typedef struct { uint32_t pc, c1, c2, n; } Ent;
typedef struct {
    unsigned long long ut, st, mf, vcs, ivcs; /* (the system's counts at the last report: user / system ticks, page faults, switches) */
    Ent *e;
    uint32_t *prev; /* (the counts at the last report) */
    uint32_t lost, lostPrev, total, totalPrev;
    pid_t tid;
    int used;
    volatile int btN;  /* (a snapshot of the calls: written by the thread itself on SIGURG, -1 asked) */
    uintptr_t bt[24];
} Slot;
static Slot sSlot[SLOTS];
static int sSlots;
static __thread int tSlot = -1;
static __thread uintptr_t tLo, tHi;
static int sOff = -1;

/* this program's code as mapped (r-xp in /proc/self/maps: __executable_start is the start of the read-only part
   before it, with a gap that is not mapped) */
static uintptr_t sTextLo = 1, sTextHi = 0;
static inline int in_text(uintptr_t a) { return a >= sTextLo && a < sTextHi; }
static void text_range(void) {
    FILE *f = fopen("/proc/self/maps", "r");
    char line[512];
    uintptr_t me = (uintptr_t)&text_range;
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        unsigned long lo, hi;
        char perm[8];
        if (sscanf(line, "%lx-%lx %7s", &lo, &hi, perm) == 3 && me >= lo && me < hi && perm[2] == 'x') {
            sTextHi = (uintptr_t)hi;
            sTextLo = (uintptr_t)lo;
            break;
        }
    }
    fclose(f);
}
/* a return address into this program: the instruction before it is a call */
static inline int ret_addr(uintptr_t a) {
    uint32_t i;
    if ((a & 3) != 0 || !in_text(a) || a < sTextLo + 4) {
        return 0;
    }
    i = *(const uint32_t *)(a - 4);
#ifdef __aarch64__
    return (i & 0xFC000000u) == 0x94000000u || (i & 0xFFFFFC1Fu) == 0xD63F0000u; /* (bl, blr) */
#endif
    return (i & 0x0F000000u) == 0x0B000000u || (i & 0xFE000000u) == 0xFA000000u || (i & 0x0FFFFFF0u) == 0x012FFF30u;
}
#if !defined(__arm__) && !defined(__aarch64__) /* (the tests built for the PC: nothing noted) */
static void note(void *ctx, uint32_t w) { (void)ctx; (void)w; }
#else
static void note(void *ctx, uint32_t w) {
    ucontext_t *uc = (ucontext_t *)ctx;
    Slot *s;
#ifdef __aarch64__
    uintptr_t pc = uc->uc_mcontext.pc, lr = uc->uc_mcontext.regs[30], fp = uc->uc_mcontext.regs[29], sp = uc->uc_mcontext.sp;
#else
    uintptr_t pc = uc->uc_mcontext.arm_pc, lr = uc->uc_mcontext.arm_lr, fp = uc->uc_mcontext.arm_fp, sp = uc->uc_mcontext.arm_sp;
#endif
    uint32_t c1 = 0, c2 = 0, h, k;
    if (tSlot < 0 || (s = &sSlot[tSlot])->e == NULL) {
        return;
    }
    if (in_text(pc)) {
        /* (this program's frames: fp points at the saved fp, the return address above it) */
        uintptr_t r1 = 0, r2 = 0;
        if (fp >= sp && fp + 2 * sizeof(uintptr_t) <= tHi && (fp & 3) == 0) {
            r1 = ((const uintptr_t *)fp)[1];
            uintptr_t f2 = ((const uintptr_t *)fp)[0];
            if (f2 > fp && f2 + 2 * sizeof(uintptr_t) <= tHi && (f2 & 3) == 0) {
                r2 = ((const uintptr_t *)f2)[1];
            }
        }
        if (ret_addr(lr & ~(uintptr_t)1) && lr != r1) { /* (a function without a frame of its own: lr is its caller) */
            r2 = r1;
            r1 = lr;
        }
        c1 = ret_addr(r1) ? (uint32_t)r1 : 0;
        c2 = ret_addr(r2) ? (uint32_t)r2 : 0;
    } else if (sp >= tLo && sp < tHi) {
        /* (in a library: the first two return addresses into this program on the stack) */
        const uintptr_t *p = (const uintptr_t *)sp, *end = (const uintptr_t *)tHi;
        int n = 0;
        if (ret_addr(lr)) {
            c1 = (uint32_t)lr;
        }
        for (; p < end && n < 2048 && c2 == 0; p++, n++) {
            if (ret_addr(*p)) {
                if (c1 == 0) {
                    c1 = (uint32_t)*p;
                } else if (*p != c1) {
                    c2 = (uint32_t)*p;
                }
            }
        }
    }
    s->total += w;
    h = ((uint32_t)pc * 2654435761u) ^ (c1 * 40503u) ^ (c2 * 7u);
    for (k = 0; k < 32; k++) {
        Ent *e = &s->e[(h + k) & (ENTRIES - 1)];
        if (e->n == 0) {
            e->pc = (uint32_t)pc;
            e->c1 = c1;
            e->c2 = c2;
            __sync_synchronize();
            e->n = w;
            return;
        }
        if (e->pc == (uint32_t)pc && e->c1 == c1 && e->c2 == c2) {
            e->n += w;
            return;
        }
    }
    s->lost += w;
}
#endif
/* where a thread is now (one that waits gets no SIGPROF): pc, lr, then the frame records (fp -> saved fp, return) */
static void on_bt(int sig, siginfo_t *si, void *ctx) {
    int e = errno, n = 0;
    Slot *s;
    (void)sig; (void)si;
    if (tSlot < 0) {
        return;
    }
    s = &sSlot[tSlot];
#ifdef __aarch64__
    {
        ucontext_t *uc = (ucontext_t *)ctx;
        uintptr_t fp = uc->uc_mcontext.regs[29];
        s->bt[n++] = uc->uc_mcontext.pc;
        s->bt[n++] = uc->uc_mcontext.regs[30];
        while (n < 24 && fp >= tLo && fp + 16 <= tHi && (fp & 7) == 0) {
            uintptr_t next = ((const uintptr_t *)fp)[0], ret = ((const uintptr_t *)fp)[1];
            s->bt[n++] = ret;
            if (next <= fp) {
                break;
            }
            fp = next;
        }
    }
#else
    (void)ctx;
#endif
    __sync_synchronize();
    s->btN = n;
    errno = e;
}
static void on_sig(int sig, siginfo_t *si, void *ctx) {
    int e = errno;
    /* (the kernel looks at a thread's processor time at its clock ticks, every 4 or 10 ms: one signal then for the
       milliseconds since the last, the others counted as overruns) */
    int w = 1 + (si != NULL && si->si_overrun > 0 ? si->si_overrun : 0);
    (void)sig;
    note(ctx, (uint32_t)(w > 100 ? 100 : w));
    errno = e;
}

/* ---- names ---- */
static uint32_t *sSymA, *sSymS;
static char **sSymN;
static int sSymCount;
static void syms_load(void) {
    FILE *f = fopen("bt3.sym", "r");
    char line[512];
    int cap = 0;
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char *sp = strchr(line, ' '), *nl;
        if (sp == NULL || (sp = strchr(sp + 1, ' ')) == NULL) { /* (address, size, name) */
            continue;
        }
        if ((nl = strchr(sp + 1, '\n')) != NULL) {
            *nl = 0;
        }
        if (sSymCount == cap) {
            cap = cap ? cap * 2 : 16384;
            sSymA = (uint32_t *)realloc(sSymA, (size_t)cap * sizeof(uint32_t));
            sSymN = (char **)realloc(sSymN, (size_t)cap * sizeof(char *));
            sSymS = (uint32_t *)realloc(sSymS, (size_t)cap * sizeof(uint32_t));
        }
        sSymA[sSymCount] = (uint32_t)strtoul(line, &nl, 16);
        sSymS[sSymCount] = (uint32_t)strtoul(nl, NULL, 16);
        sSymN[sSymCount] = strdup(sp + 1);
        sSymCount++;
    }
    fclose(f);
}
static const char *intern(const char *s) {
    static char *pool[1024];
    static int n;
    int i;
    for (i = 0; i < n; i++) {
        if (strcmp(pool[i], s) == 0) {
            return pool[i];
        }
    }
    if (n == 1024) {
        return "(many)";
    }
    pool[n] = strdup(s);
    return pool[n++];
}
static const char *name_of(uint32_t a) {
    char b[200];
    if (a == 0) {
        return NULL;
    }
    if (in_text(a)) {
        int lo = 0, hi = sSymCount - 1, m;
        if (sSymCount == 0 || a < sSymA[0]) {
            snprintf(b, sizeof(b), "bt3+%x", (unsigned)(a & ~0xFFFu));
            return intern(b);
        }
        while (lo < hi) { /* (the last symbol at or before a) */
            m = (lo + hi + 1) / 2;
            if (sSymA[m] <= a) {
                lo = m;
            } else {
                hi = m - 1;
            }
        }
        /* (past the end of a function with a known size: the stubs that call the libraries, or code without a name) */
        if (sSymS[lo] != 0 && a - sSymA[lo] >= sSymS[lo]) {
            snprintf(b, sizeof(b), "bt3+%x (after %s)", (unsigned)(a & ~0xFFFu), sSymN[lo]);
            return intern(b);
        }
        return sSymN[lo];
    } else {
        Dl_info di;
        const char *lib = "?", *sl;
        memset(&di, 0, sizeof(di));
        if (dladdr((void *)(uintptr_t)a, &di) && di.dli_fname != NULL) {
            lib = (sl = strrchr(di.dli_fname, '/')) != NULL ? sl + 1 : di.dli_fname;
        }
        snprintf(b, sizeof(b), "[%s]%s%s", lib, di.dli_sname ? " " : "", di.dli_sname ? di.dli_sname : "");
        return intern(b);
    }
}

/* ---- the report ---- */
typedef struct { const char *a, *b; uint32_t n; } Agg;
static int agg_add(Agg *g, int n, int cap, const char *a, const char *b, uint32_t c) {
    int i;
    for (i = 0; i < n; i++) {
        if (g[i].a == a && g[i].b == b) {
            g[i].n += c;
            return n;
        }
    }
    if (n < cap) {
        g[n].a = a;
        g[n].b = b;
        g[n].n = c;
        n++;
    }
    return n;
}
static int agg_cmp(const void *x, const void *y) {
    uint32_t a = ((const Agg *)x)->n, b = ((const Agg *)y)->n;
    return a < b ? 1 : a > b ? -1 : 0;
}
static void clocks(FILE *f) {
    char path[160], v[64];
    int c;
    fprintf(f, "clocks (MHz):");
    for (c = 0; c < 4; c++) {
        FILE *g;
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", c);
        if ((g = fopen(path, "r")) != NULL) {
            if (fgets(v, sizeof(v), g) != NULL) {
                fprintf(f, " cpu%d %ld", c, atol(v) / 1000);
            }
            fclose(g);
        }
    }
    {
        FILE *p = popen("cat /sys/class/devfreq/*/cur_freq 2>/dev/null", "r");
        if (p != NULL) {
            int k = 0;
            while (fgets(v, sizeof(v), p) != NULL) {
                fprintf(f, " devfreq%d %ld", k++, atol(v) / 1000000);
            }
            pclose(p);
        }
    }
    {
        FILE *g = fopen("/proc/loadavg", "r");
        if (g != NULL) {
            if (fgets(v, sizeof(v), g) != NULL) {
                char *nl = strchr(v, '\n');
                if (nl) { *nl = 0; }
                fprintf(f, " | load %s", v);
            }
            fclose(g);
        }
    }
    fprintf(f, "\n");
}
#define AGG 3000
static Agg sSelf[AGG], sIncl[AGG];
/* the system's own counts for a thread: user and system time (ticks), minor page faults, context switches */
static void task_stat(pid_t tid, unsigned long long *ut, unsigned long long *st, unsigned long long *mf, unsigned long long *vcs, unsigned long long *ivcs) {
    char path[64], b[1024], *p;
    FILE *g;
    int field;
    *ut = *st = *mf = *vcs = *ivcs = 0;
    snprintf(path, sizeof(path), "/proc/self/task/%d/stat", (int)tid);
    if ((g = fopen(path, "r")) != NULL) {
        if (fgets(b, sizeof(b), g) != NULL && (p = strrchr(b, ')')) != NULL) {
            /* after the name: state is field 3; minflt 10, utime 14, stime 15 */
            for (field = 2; *p && field < 15;) {
                if (*p == ' ') {
                    field++;
                    if (field == 10) { *mf = strtoull(p + 1, NULL, 10); }
                    if (field == 14) { *ut = strtoull(p + 1, NULL, 10); }
                    if (field == 15) { *st = strtoull(p + 1, NULL, 10); }
                }
                p++;
            }
        }
        fclose(g);
    }
    snprintf(path, sizeof(path), "/proc/self/task/%d/status", (int)tid);
    if ((g = fopen(path, "r")) != NULL) {
        while (fgets(b, sizeof(b), g) != NULL) {
            if (strncmp(b, "voluntary_ctxt_switches:", 24) == 0) { *vcs = strtoull(b + 24, NULL, 10); }
            if (strncmp(b, "nonvoluntary_ctxt_switches:", 27) == 0) { *ivcs = strtoull(b + 27, NULL, 10); }
        }
        fclose(g);
    }
}
static void report(FILE *f, double t0, double t1) {
    int i, k;
    double secs = t1 - t0;
    fprintf(f, "=== %.0f..%.0f s ===\n", t0, t1);
    clocks(f);
    { extern void R36_GlTimes(FILE *f, double secs); R36_GlTimes(f, secs); }
    for (i = 0; i < sSlots; i++) {
        Slot *s = &sSlot[i];
        uint32_t tot = s->total - s->totalPrev, ns = 0, ni = 0, lost = s->lost - s->lostPrev;
        char name[32] = "?", path[64];
        FILE *g;
        if (s->e == NULL || tot == 0) {
            continue;
        }
        s->totalPrev += tot;
        s->lostPrev += lost;
        snprintf(path, sizeof(path), "/proc/self/task/%d/comm", (int)s->tid);
        if ((g = fopen(path, "r")) != NULL) {
            if (fgets(name, sizeof(name), g) != NULL) {
                char *nl = strchr(name, '\n');
                if (nl) { *nl = 0; }
            }
            fclose(g);
        }
        for (k = 0; k < ENTRIES; k++) {
            Ent *e = &s->e[k];
            uint32_t n = e->n, d = n - s->prev[k];
            const char *a, *c1, *c2;
            if (d == 0) {
                continue;
            }
            s->prev[k] = n;
            a = name_of(e->pc);
            c1 = name_of(e->c1);
            c2 = name_of(e->c2);
            ns = agg_add(sSelf, ns, AGG, a, in_text(e->pc) ? NULL : c1, d);
            ni = agg_add(sIncl, ni, AGG, a, NULL, d);
            if (c1 != NULL && c1 != a) {
                ni = agg_add(sIncl, ni, AGG, c1, NULL, d);
            }
            if (c2 != NULL && c2 != a && c2 != c1) {
                ni = agg_add(sIncl, ni, AGG, c2, NULL, d);
            }
        }
        if (tot * 100 < (uint32_t)(secs * 1000) * 2) { /* (under 2% of a core: only the line) */
            fprintf(f, "thread %s (%d): %u ms\n", name, (int)s->tid, tot);
            continue;
        }
        {
            unsigned long long ut, st, mf, vcs, ivcs;
            long hz = sysconf(_SC_CLK_TCK);
            task_stat(s->tid, &ut, &st, &mf, &vcs, &ivcs);
            fprintf(f, "  (the system: %.0f ms in the program, %.0f ms in the kernel, %llu page faults, %llu waits, %llu times put aside)\n",
                    (ut - s->ut) * 1000.0 / hz, (st - s->st) * 1000.0 / hz, mf - s->mf, vcs - s->vcs, ivcs - s->ivcs);
            s->ut = ut; s->st = st; s->mf = mf; s->vcs = vcs; s->ivcs = ivcs;
        }
        fprintf(f, "thread %s (%d): %u ms of processor in %.0f s (%.0f%% of a core)%s\n", name, (int)s->tid, tot, secs,
                tot / (secs * 10.0), lost ? " (some samples not counted)" : "");
        qsort(sSelf, ns, sizeof(Agg), agg_cmp);
        qsort(sIncl, ni, sizeof(Agg), agg_cmp);
        fprintf(f, "  self:\n");
        for (k = 0; k < (int)ns && k < 30 && sSelf[k].n * 200 >= tot; k++) {
            fprintf(f, "  %5.1f%% %s%s%s\n", sSelf[k].n * 100.0 / tot, sSelf[k].a, sSelf[k].b ? " <- " : "", sSelf[k].b ? sSelf[k].b : "");
        }
        fprintf(f, "  with what they call (two levels):\n");
        for (k = 0; k < (int)ni && k < 30 && sIncl[k].n * 100 >= tot; k++) {
            fprintf(f, "  %5.1f%% %s\n", sIncl[k].n * 100.0 / tot, sIncl[k].a);
        }
    }
    fflush(f);
}
/* (each report) the calls the game's threads are in right now */
static void snapshots(FILE *f) {
    int i, k;
    for (i = 0; i < sSlots; i++) {
        Slot *s = &sSlot[i];
        char name[32] = "?", path[64];
        FILE *g;
        snprintf(path, sizeof(path), "/proc/self/task/%d/comm", (int)s->tid);
        if ((g = fopen(path, "r")) == NULL) {
            continue;
        }
        if (fgets(name, sizeof(name), g) != NULL) {
            name[strcspn(name, "\n")] = 0;
        }
        fclose(g);
        if (strncmp(name, "bt3_", 4) != 0 || strcmp(name, "bt3_prof") == 0) {
            continue;
        }
        s->btN = -1;
        __sync_synchronize();
        if (syscall(SYS_tgkill, getpid(), s->tid, SIGURG) != 0) {
            continue;
        }
        for (k = 0; k < 50 && s->btN < 0; k++) {
            struct timespec d = {0, 2000000L};
            nanosleep(&d, NULL);
        }
        if (s->btN <= 0) {
            fprintf(f, "  now: thread %s (%d): no answer\n", name, (int)s->tid);
            continue;
        }
        fprintf(f, "  now: thread %s (%d):", name, (int)s->tid);
        for (k = 0; k < s->btN; k++) {
            const char *nm = s->bt[k] >> 32 || !in_text(s->bt[k]) ? NULL : name_of((uint32_t)s->bt[k]);
            if (nm != NULL) {
                fprintf(f, " %s%s", k ? "<- " : "", nm);
            } else {
                Dl_info di;
                memset(&di, 0, sizeof(di));
                fprintf(f, " %s%s", k ? "<- " : "", dladdr((void *)s->bt[k], &di) && di.dli_sname ? di.dli_sname : "(lib)");
            }
        }
        fprintf(f, "\n");
    }
}
static double now_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
static double sStart;
volatile unsigned gR36ProfWindow; /* (the report's number: vita_vgl.c's effects A/B goes by it) */
static void *dump_thread(void *arg) {
    FILE *f = fopen("prof.txt", "w");
    double t0 = now_s() - sStart;
    (void)arg;
    if (f == NULL) {
        return NULL;
    }
    syms_load();
    fprintf(f, "r36 prof: %d names from bt3.sym; per thread, where its processor time went (1 sample = 1 ms)\n", sSymCount);
    fflush(f);
    for (;;) {
        double t1;
        struct timespec d = {10, 0};
        while (nanosleep(&d, &d) != 0 && errno == EINTR) {
        }
        t1 = now_s() - sStart;
        report(f, t0, t1);
        snapshots(f);
        {   /* the draws by where they were asked for (r36s_gl.c), the most first */
            extern unsigned gR36SiteA[64], gR36SiteN[64];
            extern unsigned long long gR36SiteNs[64];
            static unsigned pn[64];
            static unsigned long long pns[64];
            unsigned dn[64], idx[64], j, k, m = 0;
            unsigned long long dns[64];
            double secs = 10.0;
            for (j = 0; j < 64; j++) {
                dn[j] = gR36SiteN[j] - pn[j]; pn[j] = gR36SiteN[j];
                dns[j] = gR36SiteNs[j] - pns[j]; pns[j] = gR36SiteNs[j];
                if (dn[j] != 0) {
                    idx[m++] = j;
                }
            }
            for (j = 1; j < m; j++) {
                unsigned x = idx[j];
                int q = (int)j - 1;
                while (q >= 0 && dns[idx[q]] < dns[x]) {
                    idx[q + 1] = idx[q];
                    q--;
                }
                idx[q + 1] = x;
            }
            fprintf(f, "  draws by where asked (a second: count, driver ms):");
            for (k = 0; k < m && k < 14; k++) {
                const char *nm = name_of(gR36SiteA[idx[k]]);
                fprintf(f, " %s %.0f %.0f;", nm ? nm : "?", dn[idx[k]] / secs, dns[idx[k]] / 1e6 / secs);
            }
            fprintf(f, "\n");
        }
        { extern void R36_GameState(FILE *f); R36_GameState(f); }
        gR36ProfWindow++;
        fflush(f);
        t0 = t1;
    }
    return NULL;
}

static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
static int off(void) {
    if (sOff < 0) {
        const char *e = getenv("R36_PROF");
        sOff = e != NULL && atoi(e) == 0;
    }
    return sOff;
}
/* this thread: a table and a timer of its own (each thread of ours, from build.py's pthread_create wrapper; the main
   thread at start) */
void R36_ProfThread(void) {
    static int handler;
    struct sigevent ev;
    timer_t tm;
    struct itimerspec it;
    pthread_attr_t a;
    int slot;
    if (tSlot >= 0 || off()) {
        return;
    }
    pthread_mutex_lock(&sLock);
    if (!handler) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = on_sig;
        sa.sa_flags = SA_SIGINFO | SA_RESTART;
        text_range();
        sigaction(SIGPROF, &sa, NULL);
        sa.sa_sigaction = on_bt;
        sigaction(SIGURG, &sa, NULL);
        sStart = now_s();
        handler = 1;
    }
    slot = sSlots < SLOTS ? sSlots++ : -1;
    pthread_mutex_unlock(&sLock);
    if (slot < 0) {
        return;
    }
    if (pthread_getattr_np(pthread_self(), &a) == 0) {
        void *lo = NULL;
        size_t size = 0;
        pthread_attr_getstack(&a, &lo, &size);
        pthread_attr_destroy(&a);
        tLo = (uintptr_t)lo;
        tHi = (uintptr_t)lo + size;
    }
    sSlot[slot].e = (Ent *)calloc(ENTRIES, sizeof(Ent));
    sSlot[slot].prev = (uint32_t *)calloc(ENTRIES, sizeof(uint32_t));
    sSlot[slot].tid = (pid_t)syscall(SYS_gettid);
    if (sSlot[slot].e == NULL || sSlot[slot].prev == NULL) {
        return;
    }
    tSlot = slot;
    memset(&ev, 0, sizeof(ev));
    ev.sigev_notify = 4; /* SIGEV_THREAD_ID */
    ev.sigev_signo = SIGPROF;
    ev._sigev_un._tid = sSlot[slot].tid;
    if (timer_create(CLOCK_THREAD_CPUTIME_ID, &ev, &tm) != 0) {
        tSlot = -1;
        return;
    }
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_nsec = 1000000;
    it.it_value = it.it_interval;
    timer_settime(tm, 0, &it, NULL);
}
/* the report's thread (once, after the system's libraries are started) */
void R36_ProfStart(void) {
    static int started;
    pthread_t t;
    if (off() || __sync_lock_test_and_set(&started, 1)) {
        return;
    }
    if (pthread_create(&t, NULL, dump_thread, NULL) == 0) {
        pthread_detach(t);
        pthread_setname_np(t, "r36_prof");
        R36_LogLine("r36: where the time goes: prof.txt every 10 s\n");
    }
}
