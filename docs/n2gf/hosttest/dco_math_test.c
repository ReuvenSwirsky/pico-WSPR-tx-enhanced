/* Host-side model of pico-hf-oscillator piodco.c (PioDCOSetFreq, PioDCOWorker2,
 * PioDCOGetFreqShiftMilliHertz) and TxChannel.c's call into it, plus the dco2.pio timing.
 * Build: cc -O2 -Wall -o dco_math_test dco_math_test.c -lm
 * Arithmetic is copied verbatim (types included) from the firmware.            */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#define PIOASM_DELAY_CYCLES 4
#define WSPR_FREQ_STEP_MILHZ 2930UL
#define MHz 1000000UL

/* piodco.c:123-127 (sysclk passed in instead of PLL_SYS_MHZ macro) */
static int32_t set_freq(uint32_t sys_mhz, uint32_t ui32_frq_hz, int32_t ui32_frq_millihz, int32_t *cycles_per_pi)
{
    const int64_t i64denominator = 2000LL * (int64_t)ui32_frq_hz + (int64_t)ui32_frq_millihz;
    int32_t frq_cycles_per_pi = (int32_t)(((int64_t)(sys_mhz * MHz) * (int64_t)(1<<24) * 1000LL
                                         +(i64denominator>>1)) / i64denominator);
    if (cycles_per_pi) *cycles_per_pi = frq_cycles_per_pi;
    return frq_cycles_per_pi - (PIOASM_DELAY_CYCLES<<24);   /* si32precise_cycles */
}

/* piodco.c:140-165 with a given ppb */
static int32_t freq_shift_mhz(int64_t ppb, uint64_t u64_desired_frq_millihz)
{
    int64_t i64_last_correction = ppb;
    if (!i64_last_correction) return 0;
    int64_t i64corr_coeff = (u64_desired_frq_millihz + 500000LL) / 1000000LL;
    int32_t i32ret_millis = (i64_last_correction * i64corr_coeff + 50000LL) / 1000000LL;
    return i32ret_millis;
}

/* Run PioDCOWorker2's loop for nwords words; each word drives 4 half-periods of
 * (y + 4) sysclk cycles in dco2.pio (out/mov/jmp-loop/set == y+4 cycles each).
 * Returns average output frequency in Hz; reports min/max y.                  */
static double run_worker2(uint32_t sys_mhz, int32_t si32precise_cycles, long nwords, uint32_t *ymin, uint32_t *ymax)
{
    int32_t i32acc_error = 0; uint32_t i32wc, i32reg;
    uint64_t cycles = 0; *ymin = 0xffffffff; *ymax = 0;
    for (long n = 0; n < nwords; n++) {
        i32reg = (uint32_t)si32precise_cycles;
        i32wc = (i32reg - i32acc_error) >> 24U;
        i32acc_error += (i32wc << 24U) - i32reg;   /* wraps like the M33/M0+ */
        if (i32wc < *ymin) *ymin = i32wc;
        if (i32wc > *ymax) *ymax = i32wc;
        cycles += 4ULL * ((uint64_t)i32wc + 4);
    }
    /* 4 half periods per word = 2 RF periods per word */
    return (2.0 * nwords) / ((double)cycles / (sys_mhz * 1e6));
}

int main(void)
{
    const uint32_t bands[] = {1838000, 3570000, 7040000, 10140100, 14097000, 18106000, 21096000, 24926000, 28126000};
    const uint32_t clocks[] = {125, 200, 270, 276, 300};
    int fails = 0;

    printf("== 1. DCO ceiling: f_max = sysclk/8 (half period >= 4 PIO cycles)\n");
    for (unsigned c = 0; c < sizeof clocks/sizeof *clocks; c++)
        printf("   sysclk %3u MHz -> f_max %.3f MHz\n", clocks[c], clocks[c] / 8.0);

    printf("\n== 2. si32precise_cycles sign and Worker2 output for every band (Tx = band + 100 Hz)\n");
    for (unsigned c = 0; c < sizeof clocks/sizeof *clocks; c++) {
        for (unsigned b = 0; b < sizeof bands/sizeof *bands; b++) {
            uint32_t f = bands[b] + 100;
            int32_t cpp; int32_t pc = set_freq(clocks[c], f, 0, &cpp);
            uint32_t ymin, ymax;
            double fout = run_worker2(clocks[c], pc, 4000000, &ymin, &ymax);
            int bad = fabs(fout - f) > 5.0;
            if (clocks[c] >= 276 && bad) fails++;
            if (bands[b] >= 21000000 || bad)
                printf("   %3u MHz  f=%9u  precise=%11d (%s)  y=[%u..%u]  avg out=%.3f Hz  %s\n",
                       clocks[c], f, pc, pc < 0 ? "NEGATIVE" : "ok", ymin, ymax, fout, bad ? "<-- WRONG" : "");
        }
    }

    printf("\n== 3. WSPR tone spacing at 28.1261 MHz, 300 MHz and 276 MHz sysclk (TxChannel.c:80-81)\n");
    uint32_t tsys[] = {276, 300};
    for (int k = 0; k < 2; k++) {
        double prev = 0;
        for (int sym = 0; sym < 4; sym++) {
            int32_t pc = set_freq(tsys[k], 28126100, (int32_t)((uint32_t)sym * WSPR_FREQ_STEP_MILHZ), NULL);
            uint32_t a, b; double fo = run_worker2(tsys[k], pc, 20000000, &a, &b);
            double ideal = 28126100 + sym * 1.4648;
            /* exact frequency implied by the quantised 2^24-scaled half period */
            double exact = tsys[k] * 1e6 / (2.0 * ((pc + (4 << 24)) / 16777216.0));
            printf("   %u MHz sym %d: quantised f=%.4f Hz  (ideal %.4f, err %+.4f Hz)  step %.4f Hz  worker avg %.3f\n",
                   tsys[k], sym, exact, ideal, exact - ideal, sym ? exact - prev : 0.0, fo);
            if (fabs(exact - ideal) > 0.3) fails++;
            prev = exact;
        }
        double lsb = 28126100.0 * 28126100.0 * 2.0 / (tsys[k] * 1e6 * 16777216.0);
        printf("   frequency resolution (1 LSB of 2^24 half-period) at 28.126 MHz, %u MHz: %.3f Hz\n", tsys[k], lsb);
    }

    printf("\n== 4. int32/int64 range of GPS ppb correction path vs frequency (piodco.c:158-159, TxChannel.c:76-81)\n");
    int64_t ppbs[] = {1, 1000, 30000, 250000, 2000000000LL, 100000000000LL};
    for (unsigned b = 0; b < sizeof bands/sizeof *bands; b++) {
        for (unsigned p = 0; p < sizeof ppbs/sizeof *ppbs; p++) {
            uint32_t f = bands[b] + 100;
            int64_t exact64 = (ppbs[p] * (int64_t)((f * 1000ULL + 500000) / 1000000) + 50000) / 1000000;
            int32_t r = freq_shift_mhz(ppbs[p], (uint64_t)(f * 1000LL));
            if ((int64_t)r != exact64)
                printf("   f=%u ppb=%lld: int32 result %d != %lld  (TRUNCATED)\n", f, (long long)ppbs[p], r, (long long)exact64);
        }
    }
    printf("   (only absurd ppb values >= ~1e11 truncate; realistic |ppb| < 250000 never do at any band)\n");
    printf("   units: result for 28.126 MHz, +1000 ppb = %d \"mHz\" (a true 1 ppm offset is 28126 mHz)\n",
           freq_shift_mhz(1000, 28126100ULL * 1000));

    printf("\n== 5. 32-bit hazards in 64-bit products used above 20 MHz\n");
    uint32_t f = 28126100;
    printf("   uint32 f*1000 would wrap: %u  vs 64-bit %llu (firmware uses 1000LL -> OK)\n",
           (uint32_t)(f * 1000u), (unsigned long long)(f * 1000ULL));
    printf("   sysclk*2^24*1000 at 300 MHz = %.3e (int64 max 9.22e18, OK; limit sysclk < 549 MHz)\n", 300e6 * 16777216.0 * 1000);

    printf("\nRESULT: %s (%d failures at >=276 MHz)\n", fails ? "FAIL" : "PASS", fails);
    return fails != 0;
}
