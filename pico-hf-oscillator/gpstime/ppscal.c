/* N2GF: safe GPS-PPS reference-frequency calibration. See ppscal.h. */
#include "ppscal.h"
#include <string.h>

/* On the Pico these run from RAM (called from the PPS and Tx-symbol ISRs). */
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#define PPSCAL_RAM(f) __not_in_flash_func(f)
#else
#define PPSCAL_RAM(f) f
#endif

void PPSCalReset(PPSCal *c)
{
    memset(c, 0, sizeof(*c));
}

static int64_t PPSCAL_RAM(div_round)(int64_t n, int64_t d)   /* d > 0 */
{
    return (n >= 0) ? (n + d / 2) / d : -((-n + d / 2) / d);
}

void PPSCAL_RAM(PPSCalOnEdge)(PPSCal *c, uint64_t now_u)
{
    const int64_t now = (int64_t)now_u;           /* signed 64-bit throughout */

    if(c->last_us)
    {
        const int64_t ivl = now - c->last_us;
        if(ivl < 1000000 - PPSCAL_IVL_TOL_US || ivl > 1000000 + PPSCAL_IVL_TOL_US)
        {
            /* missing / spurious / startup glitch: start a new unbroken run */
            ++c->n_ivl_reject;
            c->run = 0;
            c->idx = 0;
        }
    }
    c->last_us = now;

    c->ts[c->idx] = now;
    c->idx = (c->idx + 1) % (PPSCAL_WIN + 1);
    if(c->run < PPSCAL_WIN + 1)
    {
        ++c->run;
    }
    if(c->run < PPSCAL_WIN + 1)
    {
        return;                                   /* no full window yet: never compare with empty slots */
    }

    const int64_t oldest = c->ts[c->idx];         /* slot about to be overwritten = 32 intervals ago */
    const int64_t err_us = (now - oldest) - (int64_t)PPSCAL_WIN * 1000000;
    if(err_us > (int64_t)PPSCAL_WIN * PPSCAL_MAX_PPB / 1000 || err_us < -(int64_t)PPSCAL_WIN * PPSCAL_MAX_PPB / 1000)
    {
        ++c->n_win_reject;                        /* > +-100 ppm: not a crystal, reject */
        return;
    }
    const int64_t meas = div_round(err_us * 1000000000LL, (int64_t)PPSCAL_WIN * 1000000);  /* ppb */

    if(c->windows == 0)
    {
        c->iir_q8 = meas * 256;
    }
    else
    {
        const int64_t avg = div_round(c->iir_q8, 256);
        const int64_t d = meas - avg;
        if(c->windows >= PPSCAL_SETTLE && (d > PPSCAL_OUTLIER_PPB || d < -PPSCAL_OUTLIER_PPB))
        {
            ++c->n_outlier;
            if(++c->outliers_in_row < PPSCAL_RELOCK)
            {
                return;                           /* keep the last good value */
            }
            ++c->n_relock;                        /* persistent: the reference really moved */
            c->iir_q8 = meas * 256;
            c->windows = 0;
        }
        else
        {
            c->iir_q8 += (meas * 256 - c->iir_q8) / (1 << PPSCAL_IIR_SHIFT);
        }
    }
    c->outliers_in_row = 0;
    ++c->windows;

    int64_t p = div_round(c->iir_q8, 256);
    if(p > PPSCAL_MAX_PPB)  p = PPSCAL_MAX_PPB;   /* clamp what is applied */
    if(p < -PPSCAL_MAX_PPB) p = -PPSCAL_MAX_PPB;
    c->ppb = (int32_t)p;
    c->last_update_us = now_u;
    c->valid = true;
}

int PPSCAL_RAM(PPSCalGet)(const PPSCal *c, uint64_t now_us, int32_t *ppb)
{
    if(!c->valid)
    {
        return PPSCAL_NONE;
    }
    *ppb = c->ppb;
    const int64_t age = (int64_t)(now_us - c->last_update_us);   /* signed: a read racing the ISR may be "early" */
    return (age > (int64_t)PPSCAL_HOLD_US) ? PPSCAL_HELD : PPSCAL_LOCKED;
}

int32_t PPSCAL_RAM(PPSCalShiftMilliHz)(uint64_t f_millihz, int32_t ppb)
{
    if(ppb > PPSCAL_MAX_PPB)  ppb = PPSCAL_MAX_PPB;
    if(ppb < -PPSCAL_MAX_PPB) ppb = -PPSCAL_MAX_PPB;
    /* Exact: commanded = f / (1 + e)  =>  shift = f * e / (1 + e), e = ppb * 1e-9.
       |f| < 2^36 mHz (68 MHz) * 1e5 < 2^53: no overflow. Result |.| <= 6.8e6 mHz. */
    return (int32_t)div_round((int64_t)f_millihz * ppb, 1000000000LL + ppb);
}
