/* R36S: vitasdk's debugScreen interface, on the R36S's text screen (r36s_text.c) */
#ifndef R36S_DEBUGSCREEN_H
#define R36S_DEBUGSCREEN_H
int psvDebugScreenInit(void);
int psvDebugScreenFinish(void);
int psvDebugScreenPuts(const char *text);
int psvDebugScreenPrintf(const char *format, ...) __attribute__((format(printf, 1, 2)));
#endif
