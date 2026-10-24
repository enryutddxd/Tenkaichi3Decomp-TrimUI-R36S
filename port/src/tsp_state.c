#include <string.h>
/*
 * (R36S / TrimUI test builds) A few of the game's own variables, for prof.txt's report every 10 s
 * (port/r36s/shim/r36s_prof.c): where the boot-time flows stand when the game seems to wait for nothing.
 * Read at their offsets (include/sys/mcflow_a.h McFlow, include/menu/menu_i.h BootCard, include/sys/color_fade.h).
 */
#include <stdint.h>
#include <stdio.h>
#ifdef PORT_TSP /* (64 bits: the game's own pointers are 4 bytes wide) */
#define GPTR * __ptr32 __uptr
#else
#define GPTR *
#endif
extern int32_t gColorFade[4];
extern int32_t GPTR gBootCard;
extern int32_t GPTR gMcFlow;

void R36_GameState(FILE *f) {
    extern volatile unsigned gR36ProfWindow;
    extern int gVitaFxAb;
    extern int gVitaUbAb, gR36UbMode;
    extern unsigned gR36Ub[3];
    static unsigned prevUb[3];
    fprintf(f, "  UBMODE in these 10 s: %d%s | blocks put %u, ranges bound %u, KB %u\n", gVitaUbAb ? 1 + (int)(gR36ProfWindow % 4u) : gR36UbMode,
            gVitaUbAb ? " (A/B)" : "", gR36Ub[0] - prevUb[0], gR36Ub[1] - prevUb[1], (gR36Ub[2] - prevUb[2]) >> 10);
    prevUb[0] = gR36Ub[0]; prevUb[1] = gR36Ub[1]; prevUb[2] = gR36Ub[2];
    {
        extern unsigned gR36Present[3];
        static unsigned pp[3];
        fprintf(f, "  present thread: %u shown, %u dropped, %u waits\n", gR36Present[0] - pp[0], gR36Present[1] - pp[1], gR36Present[2] - pp[2]);
        memcpy(pp, gR36Present, sizeof(pp));
    }
    {
        extern int gVitaGpuAb;
        extern unsigned gVitaGlowReuse;
        static unsigned pr;
        if (gVitaGpuAb) {
            fprintf(f, "  NEW (glow every other picture, discards) in these 10 s: %s | glows reused %u\n", gR36ProfWindow % 2u == 0 ? "ON" : "off", gVitaGlowReuse - pr);
        }
        pr = gVitaGlowReuse;
    }
    extern int gVitaOptAb, gR36Opt;
    if (gVitaOptAb) {
        fprintf(f, "  OPT in these 10 s: %s\n", (gR36ProfWindow & 1) ? "off (A/B)" : "ON (A/B)");
    } else {
        fprintf(f, "  OPT: %s\n", gR36Opt ? "on" : "off");
    }
    if (gVitaFxAb) {
        fprintf(f, "  effects in these 10 s: %s\n", (gR36ProfWindow & 1) ? "LEFT OUT (A/B)" : "on");
    }
    fprintf(f, "  game: fade flags %d alpha %d step %d", (int)gColorFade[0], (int)gColorFade[2], (int)gColorFade[3]);
    if (gBootCard != NULL) {
        fprintf(f, " | boot card flags %d timer %d mc %d", (int)gBootCard[2], (int)gBootCard[3], (int)gBootCard[4]);
    }
    if (gMcFlow != NULL) {
        fprintf(f, " | card flow mode %d state %d next %d choice %d timer %d msg %d flags %x cardGone %d flow %x", (int)gMcFlow[8],
                (int)gMcFlow[9], (int)gMcFlow[7], (int)gMcFlow[10], (int)gMcFlow[11], (int)gMcFlow[12], (unsigned)gMcFlow[3],
                (int)gMcFlow[5], (unsigned)gMcFlow[0x39DE0 / 4]);
    }
    fprintf(f, "\n");
}
