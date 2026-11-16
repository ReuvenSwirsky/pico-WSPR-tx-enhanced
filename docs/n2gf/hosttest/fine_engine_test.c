/* Host test of the N2GF "fine" DCO engine: PioDCOSetFreq (WSPR_DCO_FINE branch) and
 * PioDCOWorker3 arithmetic, copied from piodco.c, driven exactly like TxChannel.c.
 * Checks: every band fits; average output frequency == requested; WSPR tone steps;
 * the scale S never changes between tones / hop offsets; per-half counts in range.
 * Build: cc -O2 -Wall -o fine_engine_test fine_engine_test.c -lm                    */
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "ppscal.h"   /* N2GF GPSCAL: real correction math (link ppscal.c) */
static uint32_t PLL_SYS_MHZ = 288; static const uint64_t MHz = 1000000;
static uint32_t dco_reg4, dco_shift;
static const uint32_t dco_spread[4] = {0x00000000u, 0x00000001u, 0x00010001u, 0x00010101u};

static int set_freq_fine(uint32_t ui32_frq_hz, int32_t ui32_frq_millihz)
{
    const int64_t i64denominator = 2000LL * (int64_t)ui32_frq_hz + (int64_t)ui32_frq_millihz;
    if(i64denominator <= 0) return -1;
    const uint64_t A   = (uint64_t)(PLL_SYS_MHZ * MHz) * 4000ULL;
    const uint64_t den = (uint64_t)i64denominator;
    const uint64_t q20 = (A << 20) / den;
    const uint64_t r20 = (A << 20) % den;
    const uint64_t c4  = q20 >> 20;
    if(c4 < 16 || c4 > 1029) return -1;
    uint32_t S = dco_shift;
    const uint64_t need = c4 - 8 + 2;
    if(S < 20 || S > 27 || (need << S) >= (1ULL << 31) || (need << (S + 2)) < (1ULL << 31)) {
        S = 27; while(S > 20 && (need << S) >= (1ULL << 31)) --S;
    }
    const uint64_t val = (q20 << (S - 20)) + (((r20 << (S - 20)) + (den >> 1)) / den);
    dco_reg4 = (uint32_t)(val - (8ULL << S));
    dco_shift = S;
    return 0;
}

/* Worker3 for nwords; returns average frequency; checks counts. */
static double worker3(long nwords, int *bad)
{
    const uint32_t reg = dco_reg4, S = dco_shift; int32_t err = 0; uint64_t cycles = 0;
    for(long n = 0; n < nwords; n++) {
        const uint32_t w = (reg - (uint32_t)err) >> S;
        err += (int32_t)((w << S) - reg);
        const uint32_t word = (w >> 2) * 0x01010101u + dco_spread[w & 3u];
        for(int i = 0; i < 4; i++) {
            uint32_t c = (word >> (8 * i)) & 0xff;
            if (w > 1023) *bad = 1;
            cycles += c + 2;                 /* dco3.pio: half period = count + 2 */
        }
        if (err > 0 || err <= -(1 << S)) *bad = 1;
        /* phase check: accumulated cycles never drift more than 1 cycle from ideal */
        if ((n & 1023) == 0) {
            double ideal = (n + 1) * ((double)reg / (double)(1u << S) + 8.0);
            if (fabs((double)cycles - ideal) > 1.0001) *bad = 1;
        }
    }
    /* exact long-run frequency implied by the programmed value (worker phase error is bounded) */
    return 2.0 * PLL_SYS_MHZ * 1e6 / ((double)reg / (double)(1u << S) + 8.0);
}

int main(void)
{
    const uint32_t bands[] = {1838000, 3570000, 7040000, 10140100, 14097000, 18106000, 21096000, 24926000, 28126000};
    int fails = 0;
    uint32_t clocks[] = {288, 300, 200, 150};
    for (unsigned c = 0; c < 4; c++) {
        PLL_SYS_MHZ = clocks[c];
        printf("== sysclk %u MHz (max RF sysclk/8 = %.3f MHz)\n", PLL_SYS_MHZ, PLL_SYS_MHZ / 8.0);
        for (unsigned b = 0; b < 9; b++) {
            dco_shift = 0;
            uint32_t base = bands[b] + 100;               /* TxChannel: dial + 100 Hz */
            if (set_freq_fine(base, 0)) { printf("   %9u Hz: refused (above limit)\n", base);
                if (bands[b] + 200 <= PLL_SYS_MHZ * 1e6 / 8) { fails++; } continue; }
            uint32_t S0 = dco_shift; double maxerr = 0, minstep = 9, maxstep = 0, prev = 0; int bad = 0, schange = 0;
            for (int hop = -90; hop <= 90; hop += 90)
                for (int sym = 0; sym < 4; sym++) {
                    set_freq_fine(base + hop, (int32_t)((uint32_t)sym * 2930UL));   /* TxChannel.c:80-81 */
                    if (dco_shift != S0) schange = 1;
                    double f = worker3(2000000, &bad);
                    double ideal = base + hop + sym * 1.465;
                    if (fabs(f - ideal) > maxerr) maxerr = fabs(f - ideal);
                    if (sym) { double st = f - prev; if (st < minstep) minstep = st; if (st > maxstep) maxstep = st; }
                    prev = f;
                }
            int ok = !bad && !schange && maxerr < 0.02 && minstep > 1.44 && maxstep < 1.49;
            if (!ok) fails++;
            printf("   %9u Hz: S=%u  max |f-ideal| %.4f Hz  tone step %.4f..%.4f Hz  S stable:%s counts ok:%s  %s\n",
                   base, S0, maxerr, minstep, maxstep, schange ? "NO" : "yes", bad ? "NO" : "yes", ok ? "PASS" : "FAIL");
        }
    }
    /* GPSCAL / CALPPM correction through the real TxChannel path:
       PioDCOSetFreq(f, sym*2930 - 2*PPSCalShiftMilliHz(f*1000, ppb)); real clock off by ppb. */
    PLL_SYS_MHZ = 288;
    printf("== sysclk 288 MHz with reference error corrected (TxChannelApplyFreq)\n");
    const int ppbs[] = {-99500, -30000, -1500, 0, 1500, 30000, 99500};   /* +-300 drift keeps inside the +-100 ppm clamp */
    for (unsigned b = 0; b < 9; b++) {
        double worst = 0; int bad = 0, schange = 0; uint32_t base = bands[b] + 100;
        for (unsigned e = 0; e < 7; e++) {
            dco_shift = 0; set_freq_fine(base, -2 * PPSCalShiftMilliHz((uint64_t)base * 1000, ppbs[e]));
            uint32_t S0 = dco_shift;
            for (int sym = 0; sym < 4; sym++) {
                /* correction drifting +-300 ppb mid-Tx must not change the scale */
                int32_t ppb = ppbs[e] + (sym & 1 ? 300 : -300);
                set_freq_fine(base, (int32_t)(sym * 2930) - 2 * PPSCalShiftMilliHz((uint64_t)base * 1000, ppb));
                if (dco_shift != S0) schange = 1;
                double fcmd = worker3(500000, &bad);
                double fout = fcmd * (1.0 + ppb * 1e-9);         /* what a clock that far off really makes */
                double err = fout - (base + sym * 1.465);
                if (fabs(err) > worst) worst = fabs(err);
            }
        }
        int ok = !bad && !schange && worst < 0.02;
        if (!ok) fails++;
        printf("   %9u Hz: +-100 ppm corrected, worst |f_out-ideal| %.4f Hz, S stable:%s  %s\n", base, worst, schange ? "NO" : "yes", ok ? "PASS" : "FAIL");
    }
    printf("\nRESULT: %s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
