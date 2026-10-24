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
        extern int gR36NoDrop, gVitaNoDropAb;
        fprintf(f, "  present thread: %u shown, %u dropped, %u waits; every picture shown: %s%s\n", gR36Present[0] - pp[0], gR36Present[1] - pp[1], gR36Present[2] - pp[2],
                gR36NoDrop ? "on" : "off", gVitaNoDropAb ? " (A/B, 10 s each)" : "");
        memcpy(pp, gR36Present, sizeof(pp));
    }
    {
        extern int gVitaGpuAb;
        extern unsigned gVitaGlowReuse;
        static unsigned pr;
        if (gVitaGpuAb) {
            static const char *kAb[3] = {"as by default", "no table passes/fog", "no outline"};
            fprintf(f, "  FX A/B in these 10 s: %s\n", kAb[gR36ProfWindow % 3u]);
        }
        pr = gVitaGlowReuse;
    }
    {
        extern unsigned gVitaAClr[3];
        static unsigned pa[3];
        fprintf(f, "  alpha clears: %u batches (%u sprites) cleared, %u looked at and drawn\n", gVitaAClr[0] - pa[0], gVitaAClr[1] - pa[1], gVitaAClr[2] - pa[2]);
        memcpy(pa, gVitaAClr, sizeof(pa));
    }
    {
        extern unsigned gR36Pool[3];
        static unsigned pq[3];
        fprintf(f, "  texture pool: %u taken from it, %u kept, %u made new\n", gR36Pool[0] - pq[0], gR36Pool[1] - pq[1], gR36Pool[2] - pq[2]);
        memcpy(pq, gR36Pool, sizeof(pq));
    }
    {
        extern unsigned gVitaPsw[4], gVitaStBkN;
        extern int gVitaStBk, gVitaStBkAb;
        static unsigned pw[4], pn;
        extern int gR36UbLim, gVitaFxPrev, gVitaFxPrevAb;
        extern unsigned gVitaFxEarly[2];
        static unsigned pe[2];
        fprintf(f, "  ids/fog from the picture before: %s%s, %u ids and %u fog passes made early\n", gVitaFxPrev ? "on" : "off", gVitaFxPrevAb ? " (A/B, 10 s each)" : "",
                gVitaFxEarly[0] - pe[0], gVitaFxEarly[1] - pe[1]);
        memcpy(pe, gVitaFxEarly, sizeof(pe));
        fprintf(f, "  stage strips on the batched program: %s%s, %u draws; uniform slots in use only: %s\n", gVitaStBk ? "on" : "off", gVitaStBkAb ? " (A/B, 10 s each)" : "",
                gVitaStBkN - pn, gR36UbLim ? "on" : "off");
        pn = gVitaStBkN;
        fprintf(f, "  program changes: %u; GS <-> parts, same pixel shader %u, differing only by 16/64/1024 %u; within one side %u\n",
                gVitaPsw[0] - pw[0], gVitaPsw[1] - pw[1], gVitaPsw[2] - pw[2], gVitaPsw[3] - pw[3]);
        memcpy(pw, gVitaPsw, sizeof(pw));
        {   /* the commonest changes (kind:flags -> kind:flags), then the table emptied */
            extern unsigned gVitaPswPair[64][2];
            int j, t;
            fprintf(f, "  program changes, commonest (kind:flags>kind:flags count):");
            for (t = 0; t < 10; t++) {
                int best = -1;
                for (j = 0; j < 64; j++) {
                    if (gVitaPswPair[j][1] != 0 && (best < 0 || gVitaPswPair[j][1] > gVitaPswPair[best][1])) {
                        best = j;
                    }
                }
                if (best < 0) {
                    break;
                }
                fprintf(f, " %u:%x>%u:%x %u;", gVitaPswPair[best][0] >> 28, (gVitaPswPair[best][0] >> 16) & 0xFFF, (gVitaPswPair[best][0] >> 12) & 15,
                        gVitaPswPair[best][0] & 0xFFF, gVitaPswPair[best][1]);
                gVitaPswPair[best][1] = 0; /* (printed) */
            }
            fprintf(f, "\n");
            memset(gVitaPswPair, 0, sizeof(gVitaPswPair));
        }
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
