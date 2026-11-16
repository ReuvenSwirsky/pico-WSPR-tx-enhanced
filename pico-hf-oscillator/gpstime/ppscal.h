/* N2GF: safe GPS-PPS reference-frequency calibration.
 * Replaces the RPiks sliding-window code (GPStime.c, compiled out as FIX_BUGS_IN_THIS),
 * whose  "uint64 period - int64 nominal"  wraps to ~5.8e17 whenever the Pico crystal
 * runs slow (see hosttest/upstream_pps_test.c).
 * Pure C, no SDK dependency, so it is unit-tested on the host (hosttest/ppscal_test.c). */
#ifndef PPSCAL_H_
#define PPSCAL_H_
#include <stdint.h>
#include <stdbool.h>

#define PPSCAL_WIN          32          /* PPS intervals per measurement window */
#define PPSCAL_IVL_TOL_US   150         /* one interval must be 1 s +-(100 ppm + 50 us latency) */
#define PPSCAL_MAX_PPB      100000      /* +-100 ppm sanity window and clamp */
#define PPSCAL_OUTLIER_PPB  2000        /* once settled, reject windows >2 ppm from the average */
#define PPSCAL_SETTLE       8           /* windows before the outlier test is applied */
#define PPSCAL_RELOCK       10          /* this many outliers in a row = real change, restart */
#define PPSCAL_IIR_SHIFT    4           /* IIR weight 1/16 per (1 s sliding) window */
#define PPSCAL_HOLD_US      3000000ULL  /* no update for 3 s => "holding" the last good value */

typedef struct {
    int64_t  ts[PPSCAL_WIN + 1];        /* timestamps of the current unbroken PPS run */
    uint32_t idx;                       /* next write slot (= oldest entry when run is full) */
    uint32_t run;                       /* entries in the current run, capped at WIN+1 */
    int64_t  last_us;                   /* previous accepted edge, 0 = none */
    int64_t  iir_q8;                    /* smoothed error, ppb * 256 */
    uint32_t windows;                   /* windows accepted into the IIR */
    uint32_t outliers_in_row;
    volatile int32_t  ppb;              /* published correction: +ve = Pico clock fast */
    volatile bool     valid;            /* at least one full window has been measured */
    volatile uint64_t last_update_us;
    uint32_t n_ivl_reject, n_win_reject, n_outlier, n_relock;
} PPSCal;

enum { PPSCAL_NONE = 0, PPSCAL_LOCKED = 1, PPSCAL_HELD = 2 };

void PPSCalReset(PPSCal *c);
/* Call once per accepted PPS edge with the local microsecond timer. */
void PPSCalOnEdge(PPSCal *c, uint64_t now_us);
/* Returns PPSCAL_NONE / _LOCKED / _HELD; *ppb gets the last good correction. */
int  PPSCalGet(const PPSCal *c, uint64_t now_us, int32_t *ppb);
/* Correction to subtract from a frequency, in millihertz: f * e / (1 + e), e = ppb * 1e-9. */
int32_t PPSCalShiftMilliHz(uint64_t f_millihz, int32_t ppb);

#endif
