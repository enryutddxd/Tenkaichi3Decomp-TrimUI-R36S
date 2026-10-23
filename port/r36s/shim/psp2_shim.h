/*
 * R36S: the part of the PS Vita's system interface that port/vita uses, for Linux (port/r36s/shim/r36s_psp2.c).
 *
 * The Vita's sources are compiled for the R36S as they are; their #include <psp2/...> lines find the small headers
 * next to this one, which all come here. Only what port/vita calls is declared; numbers, structures and constants
 * follow the VitaSDK's where the code depends on them (button bits, open flags).
 */
#ifndef R36S_PSP2_SHIM_H
#define R36S_PSP2_SHIM_H
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int SceUID;
typedef unsigned int SceSize;
typedef int SceSSize;
typedef long long SceOff;
typedef int SceMode;
typedef unsigned int SceUInt;
typedef unsigned int SceUInt32;
typedef int SceInt32;
typedef unsigned long long SceUInt64;
typedef long long SceInt64;
typedef unsigned char SceUInt8;
typedef int SceBool;

/* ---- kernel: threads, time, memory ---- */
typedef int (*SceKernelThreadEntry)(SceSize args, void *argp);
typedef struct SceKernelThreadInfo {
    SceSize size;
    SceUID processId;
    char name[32];
    SceUInt attr;
    int status;
    SceKernelThreadEntry entry;
    void *stack;
    int stackSize;
    int initPriority;
    int currentPriority;
    int initCpuAffinityMask;
    int currentCpuAffinityMask;
    int currentCpuId;
    int lastExecutedCpuId;
    SceUInt waitType;
    SceUID waitId;
    int exitStatus;
    SceUInt64 runClocks;
    SceUInt intrPreemptCount;
    SceUInt threadPreemptCount;
    SceUInt threadReleaseCount;
    int changeCpuCount;
    int fNotifyCallback;
    int reserved;
} SceKernelThreadInfo;

unsigned long long sceKernelGetProcessTimeWide(void);
unsigned int sceKernelGetProcessTimeLow(void);
int sceKernelDelayThread(SceUInt delay);
int sceKernelDelayThreadCB(SceUInt delay);
SceUID sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int initPriority, int stackSize, SceUInt attr,
                             int cpuAffinityMask, const void *option);
int sceKernelStartThread(SceUID thid, SceSize arglen, void *argp);
int sceKernelWaitThreadEnd(SceUID thid, int *stat, SceUInt *timeout);
int sceKernelGetThreadId(void);
int sceKernelGetThreadCurrentPriority(void);
int sceKernelDeleteThread(SceUID thid);
int sceKernelGetThreadInfo(SceUID thid, SceKernelThreadInfo *info);
int sceKernelChangeThreadCpuAffinityMask(SceUID thid, int mask);
#define SCE_KERNEL_CPU_MASK_USER_0 0x00010000
#define SCE_KERNEL_CPU_MASK_USER_1 0x00020000
#define SCE_KERNEL_CPU_MASK_USER_2 0x00040000
#define SCE_KERNEL_CPU_MASK_USER_3 0x00080000
#define SCE_KERNEL_CPU_MASK_USER_ALL 0x00070000
int sceKernelChangeThreadPriority(SceUID thid, int priority);
int sceKernelExitProcess(int res);
SceUID sceKernelCreateSema(const char *name, SceUInt attr, int initVal, int maxVal, void *option);
int sceKernelWaitSema(SceUID semaid, int signal, SceUInt *timeout);
int sceKernelSignalSema(SceUID semaid, int signal);
int sceKernelDeleteSema(SceUID semaid);
SceUID sceKernelCreateMutex(const char *name, SceUInt attr, int initCount, void *option);
int sceKernelLockMutex(SceUID mutexid, int lockCount, unsigned int *timeout);
int sceKernelUnlockMutex(SceUID mutexid, int unlockCount);
int sceKernelDeleteMutex(SceUID mutexid);
int sceKernelPowerTick(int type);
#define SCE_KERNEL_POWER_TICK_DEFAULT 0
#define SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND 1
#define SCE_KERNEL_POWER_TICK_DISABLE_OLED_OFF 4
#define SCE_KERNEL_POWER_TICK_DISABLE_OLED_DIMMING 6

typedef enum SceKernelMemBlockType {
    SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW = 0x09408060,
    SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE = 0x0C208060,
    SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW = 0x0C80D060,
    SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW = 0x0D808060,
    SCE_KERNEL_MEMBLOCK_TYPE_USER_RW = 0x0C20D060
} SceKernelMemBlockType;
SceUID sceKernelAllocMemBlock(const char *name, int type, SceSize size, void *opt);
int sceKernelGetMemBlockBase(SceUID uid, void **base);
int sceKernelFreeMemBlock(SceUID uid);
SceUID sceKernelLoadStartModule(const char *path, SceSize args, void *argp, int flags, void *option, int *status);

/* ---- files ---- */
#define SCE_O_RDONLY 0x0001
#define SCE_O_WRONLY 0x0002
#define SCE_O_RDWR (SCE_O_RDONLY | SCE_O_WRONLY)
#define SCE_O_NBLOCK 0x0004
#define SCE_O_DIROPEN 0x0008
#define SCE_O_RDLOCK 0x0010
#define SCE_O_WRLOCK 0x0020
#define SCE_O_APPEND 0x0100
#define SCE_O_CREAT 0x0200
#define SCE_O_TRUNC 0x0400
#define SCE_O_EXCL 0x0800
#define SCE_SEEK_SET 0
#define SCE_SEEK_CUR 1
#define SCE_SEEK_END 2
SceUID sceIoOpen(const char *file, int flags, SceMode mode);
int sceIoClose(SceUID fd);
int sceIoRead(SceUID fd, void *data, SceSize size);
int sceIoWrite(SceUID fd, const void *data, SceSize size);
int sceIoPread(SceUID fd, void *data, SceSize size, SceOff offset);
int sceIoPwrite(SceUID fd, const void *data, SceSize size, SceOff offset);
SceOff sceIoLseek(SceUID fd, SceOff offset, int whence);
int sceIoRemove(const char *file);
int sceIoMkdir(const char *dir, SceMode mode);
int sceIoRmdir(const char *dir);
int sceIoRename(const char *oldname, const char *newname);
/* (the Vita's paths: "ux0:data/bt3/..." is the folder the program runs in) */
const char *R36_Path(const char *path, char *buf, size_t size);

/* ---- controller ---- */
typedef enum SceCtrlButtons {
    SCE_CTRL_SELECT = 0x00000001,
    SCE_CTRL_L3 = 0x00000002,
    SCE_CTRL_R3 = 0x00000004,
    SCE_CTRL_START = 0x00000008,
    SCE_CTRL_UP = 0x00000010,
    SCE_CTRL_RIGHT = 0x00000020,
    SCE_CTRL_DOWN = 0x00000040,
    SCE_CTRL_LEFT = 0x00000080,
    SCE_CTRL_LTRIGGER = 0x00000100,
    SCE_CTRL_L2 = SCE_CTRL_LTRIGGER,
    SCE_CTRL_RTRIGGER = 0x00000200,
    SCE_CTRL_R2 = SCE_CTRL_RTRIGGER,
    SCE_CTRL_L1 = 0x00000400,
    SCE_CTRL_R1 = 0x00000800,
    SCE_CTRL_TRIANGLE = 0x00001000,
    SCE_CTRL_CIRCLE = 0x00002000,
    SCE_CTRL_CROSS = 0x00004000,
    SCE_CTRL_SQUARE = 0x00008000,
    SCE_CTRL_INTERCEPTED = 0x00010000,
    SCE_CTRL_PSBUTTON = SCE_CTRL_INTERCEPTED,
    SCE_CTRL_HEADPHONE = 0x00080000,
    SCE_CTRL_VOLUP = 0x00100000,
    SCE_CTRL_VOLDOWN = 0x00200000,
    SCE_CTRL_POWER = 0x40000000
} SceCtrlButtons;
typedef enum SceCtrlPadInputMode { SCE_CTRL_MODE_DIGITAL = 0, SCE_CTRL_MODE_ANALOG = 1, SCE_CTRL_MODE_ANALOG_WIDE = 2 } SceCtrlPadInputMode;
typedef struct SceCtrlData {
    uint64_t timeStamp;
    unsigned int buttons;
    unsigned char lx, ly, rx, ry;
    uint8_t up, right, down, left, lt, rt, l1, r1, triangle, circle, cross, square;
    uint8_t reserved[4];
} SceCtrlData;
int sceCtrlSetSamplingMode(int mode);
int sceCtrlPeekBufferPositive(int port, SceCtrlData *pad_data, int count);
int sceCtrlReadBufferPositive(int port, SceCtrlData *pad_data, int count);

/* ---- touch (the R36S has no touch panels: none are reported) ---- */
#define SCE_TOUCH_MAX_REPORT 8
typedef enum SceTouchPortType { SCE_TOUCH_PORT_FRONT = 0, SCE_TOUCH_PORT_BACK = 1, SCE_TOUCH_PORT_MAX_NUM = 2 } SceTouchPortType;
typedef enum SceTouchSamplingState { SCE_TOUCH_SAMPLING_STATE_STOP = 0, SCE_TOUCH_SAMPLING_STATE_START = 1 } SceTouchSamplingState;
typedef struct SceTouchPanelInfo {
    int16_t minAaX, minAaY, maxAaX, maxAaY;
    int16_t minDispX, minDispY, maxDispX, maxDispY;
    uint8_t minForce, maxForce;
    uint8_t reserved[30];
} SceTouchPanelInfo;
typedef struct SceTouchReport {
    uint8_t id;
    uint8_t force;
    uint16_t x;
    uint16_t y;
    int8_t reserved[8];
    uint16_t info;
} SceTouchReport;
typedef struct SceTouchData {
    uint64_t timeStamp;
    unsigned int status;
    unsigned int reportNum;
    SceTouchReport report[SCE_TOUCH_MAX_REPORT];
} SceTouchData;
int sceTouchSetSamplingState(SceUInt32 port, SceTouchSamplingState state);
int sceTouchGetPanelInfo(SceUInt32 port, SceTouchPanelInfo *info);
int sceTouchPeek(SceUInt32 port, SceTouchData *data, SceUInt32 count);
int sceTouchRead(SceUInt32 port, SceTouchData *data, SceUInt32 count);

/* ---- display ---- */
typedef enum SceDisplaySetBufSync { SCE_DISPLAY_SETBUF_IMMEDIATE = 0, SCE_DISPLAY_SETBUF_NEXTFRAME = 1 } SceDisplaySetBufSync;
#define SCE_DISPLAY_PIXELFORMAT_A8B8G8R8 0
typedef struct SceDisplayFrameBuf {
    SceSize size;
    void *base;
    unsigned int pitch;
    unsigned int pixelformat;
    unsigned int width;
    unsigned int height;
} SceDisplayFrameBuf;
int sceDisplaySetFrameBuf(const SceDisplayFrameBuf *pParam, SceDisplaySetBufSync sync);
int sceDisplayGetFrameBuf(SceDisplayFrameBuf *pParam, SceDisplaySetBufSync sync);
int sceDisplayGetVcount(void);
int sceDisplayWaitVblankStart(void);
int sceDisplayWaitVblankStartCB(void);
int sceDisplayWaitVblankStartMulti(unsigned int vcount);
int sceDisplayGetRefreshRate(float *pFps);

/* ---- power (the clocks are the system's on Linux: asked and set without effect) ---- */
int scePowerSetArmClockFrequency(int freq);
int scePowerSetBusClockFrequency(int freq);
int scePowerSetGpuClockFrequency(int freq);
int scePowerSetGpuXbarClockFrequency(int freq);
int scePowerGetArmClockFrequency(void);
int scePowerGetBusClockFrequency(void);
int scePowerGetGpuClockFrequency(void);
int scePowerGetGpuXbarClockFrequency(void);

/* ---- the processor's counters (libperf): not available ---- */
int scePerfArmPmonReset(SceUID thid);
int scePerfArmPmonSelectEvent(SceUID thid, SceUInt32 counter, SceUInt8 eventCode);
int scePerfArmPmonStart(SceUID thid);
int scePerfArmPmonStop(SceUID thid);
int scePerfArmPmonGetCounterValue(SceUID thid, SceUInt32 counter, SceUInt32 *value);
#define SCE_PERF_ARM_PMON_THREAD_ID_SELF 0
#define SCE_PERF_ARM_PMON_CYCLE_COUNTER 31

/* ---- sound ---- */
typedef enum SceAudioOutPortType { SCE_AUDIO_OUT_PORT_TYPE_MAIN = 0, SCE_AUDIO_OUT_PORT_TYPE_BGM = 1, SCE_AUDIO_OUT_PORT_TYPE_VOICE = 2 } SceAudioOutPortType;
typedef enum SceAudioOutMode { SCE_AUDIO_OUT_MODE_MONO = 0, SCE_AUDIO_OUT_MODE_STEREO = 1 } SceAudioOutMode;
#define SCE_AUDIO_OUT_MAX_VOL 32768
#define SCE_AUDIO_VOLUME_FLAG_L_CH 1
#define SCE_AUDIO_VOLUME_FLAG_R_CH 2
int sceAudioOutOpenPort(SceAudioOutPortType type, int len, int freq, SceAudioOutMode mode);
int sceAudioOutReleasePort(int port);
int sceAudioOutOutput(int port, const void *buf);
int sceAudioOutSetVolume(int port, int flag, int *vol);
int sceAudioOutSetConfig(int port, SceSize len, int freq, SceAudioOutMode mode);

/* ---- applications ---- */
int sceAppMgrLoadExec(const char *appPath, char *const argv[], void *optParam);

#ifdef __cplusplus
}
#endif
#endif
