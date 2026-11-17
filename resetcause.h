/* N2GF: boot-time reset-cause capture.
   Tells a watchdog timeout (firmware blocked > 10 s, e.g. the old "GPS hang")
   apart from a normal power-up, a RUN-pin/brown-out reset, an intentional
   reboot from the settings menu, or a reboot by the bootloader/picotool. */
#ifndef RESETCAUSE_H_
#define RESETCAUSE_H_

#include <stdbool.h>
#include <stdint.h>

void        ResetCauseCapture(void);          /* call first thing in main() */
const char *ResetCauseString(void);           /* human-readable, stable for the whole run */
bool        ResetCauseWasWatchdogTimeout(void);
uint32_t    ResetCauseWatchdogTimeouts(void); /* watchdog timeouts since last power-up/RUN reset */
void        ResetCauseMarkIntentional(void);  /* call right before a deliberate watchdog reboot */
void        ResetCausePrint(const char *prefix);

#endif
