/* N2GF: boot-time reset-cause capture. See resetcause.h. */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#if PICO_RP2350
#include "hardware/structs/powman.h"
#else
#include "hardware/structs/vreg_and_chip_reset.h"
#endif
#include "resetcause.h"

/* watchdog scratch[0..3] are free for the application (the SDK/bootrom use 4..7)
   and survive a watchdog reset; they are cleared by power-on / RUN-pin resets. */
#define RC_INTENT_REG      0
#define RC_COUNT_REG       1
#define RC_COUNT_TAG_REG   2
#define RC_INTENT_MAGIC    0x4E324746u   /* "N2GF" */
#define RC_COUNT_TAG       0x57445447u   /* marks RC_COUNT_REG as valid */

static char     s_cause[128] = "not captured";
static bool     s_wdt_timeout;
static uint32_t s_wdt_count;

void ResetCauseCapture(void)
{
    const uint32_t reason   = watchdog_hw->reason;
    const bool     wd_any   = watchdog_caused_reboot();          /* any watchdog-initiated reboot */
    const bool     wd_ours  = watchdog_enable_caused_reboot();   /* from a watchdog_enable() in this firmware */
    const bool     intended = watchdog_hw->scratch[RC_INTENT_REG] == RC_INTENT_MAGIC;
    watchdog_hw->scratch[RC_INTENT_REG] = 0;

    if (watchdog_hw->scratch[RC_COUNT_TAG_REG] != RC_COUNT_TAG)
    {
        watchdog_hw->scratch[RC_COUNT_TAG_REG] = RC_COUNT_TAG;
        watchdog_hw->scratch[RC_COUNT_REG] = 0;
    }

#if PICO_RP2350
    const uint32_t chip = powman_hw->chip_reset;
    char chipbits[64] = "";
    if (chip & POWMAN_CHIP_RESET_HAD_POR_BITS)        strcat(chipbits, " POR");
    if (chip & POWMAN_CHIP_RESET_HAD_BOR_BITS)        strcat(chipbits, " BOR");
    if (chip & POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS)    strcat(chipbits, " RUN");
    if (chip & POWMAN_CHIP_RESET_HAD_GLITCH_DETECT_BITS) strcat(chipbits, " GLITCH");
    if (chip & POWMAN_CHIP_RESET_HAD_RESCUE_BITS)     strcat(chipbits, " RESCUE");
    if (chip & POWMAN_CHIP_RESET_HAD_DP_RESET_REQ_BITS) strcat(chipbits, " DEBUG");
    if (chip & POWMAN_CHIP_RESET_HAD_HZD_SYS_RESET_REQ_BITS) strcat(chipbits, " SYSRESETREQ");
    if (chip & (POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_RSM_BITS |
                POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_SWCORE_BITS |
                POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_BITS |
                POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_ASYNC_BITS)) strcat(chipbits, " WDOG");
    const bool power_up = chip & (POWMAN_CHIP_RESET_HAD_POR_BITS | POWMAN_CHIP_RESET_HAD_BOR_BITS);
    const bool run_pin  = chip & POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS;
#else
    const uint32_t chip = vreg_and_chip_reset_hw->chip_reset;
    char chipbits[64] = "";
    if (chip & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_POR_BITS)         strcat(chipbits, " POR/BOR");
    if (chip & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_RUN_BITS)         strcat(chipbits, " RUN");
    if (chip & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_PSM_RESTART_BITS) strcat(chipbits, " DEBUG");
    const bool power_up = chip & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_POR_BITS;
    const bool run_pin  = chip & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_RUN_BITS;
#endif

    const char *what;
    if (wd_ours && intended)
        what = "intentional reboot (settings REBOOT/band change)";
    else if (wd_ours && (reason & WATCHDOG_REASON_TIMER_BITS))
    {
        s_wdt_timeout = true;
        watchdog_hw->scratch[RC_COUNT_REG]++;
        what = "WATCHDOG TIMEOUT - firmware was blocked >10 s (watchdog theory confirmed)";
    }
    else if (wd_any || reason)
        what = "watchdog reboot by bootloader/picotool (e.g. after flashing)";
    else if (power_up)
        what = "power-on / brown-out";
    else if (run_pin)
        what = "RUN pin / reset button";
    else
        what = "normal (no watchdog)";

    s_wdt_count = watchdog_hw->scratch[RC_COUNT_REG];
    snprintf(s_cause, sizeof s_cause, "%s [wd reason 0x%lx, chip 0x%08lx%s%s]",
             what, (unsigned long)reason, (unsigned long)chip,
             chipbits[0] ? ":" : "", chipbits);
}

const char *ResetCauseString(void)          { return s_cause; }
bool        ResetCauseWasWatchdogTimeout(void) { return s_wdt_timeout; }
uint32_t    ResetCauseWatchdogTimeouts(void)   { return s_wdt_count; }

void ResetCauseMarkIntentional(void)
{
    watchdog_hw->scratch[RC_INTENT_REG] = RC_INTENT_MAGIC;
}

void ResetCausePrint(const char *prefix)
{
    printf("%sLast reset: %s. Watchdog timeouts since power-up: %lu\n",
           prefix ? prefix : "", s_cause, (unsigned long)s_wdt_count);
}
