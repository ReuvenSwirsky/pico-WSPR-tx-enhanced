///////////////////////////////////////////////////////////////////////////////
//
//  Roman Piksaykin [piksaykin@gmail.com], R2BDY
//  https://www.qrz.com/db/r2bdy
//
///////////////////////////////////////////////////////////////////////////////
//
//
//  piodco.c - Digital controlled radio freq oscillator based on PIO.
// 
//
//  DESCRIPTION
//
//      The oscillator provides precise generation of any frequency ranging
//  from 1 Hz to 33.333 MHz with tenth's of millihertz resolution (please note that
//  this is relative resolution owing to the fact that the absolute accuracy of 
//  onboard crystal of pi pico is limited; the absoulte accuracy can be provided
//  when using GPS reference option included).
//      The DCO uses phase locked loop principle programmed in C and PIO asm.
//      The DCO does *NOT* use any floating point operations - all time-critical
//  instructions run in 1 CPU cycle.
//      Currently the upper freq. limit is about 33.333 MHz and it is achieved only
//  using pi pico overclocking to 270 MHz.
//      Owing to the meager frequency step, it is possible to use 3, 5, or 7th
//  harmonics of generated frequency. Such solution completely cover all HF and
//  a portion of VHF band up to about 233 MHz.
//      Unfortunately due to pure digital freq.synthesis principle the jitter may
//  be a problem on higher frequencies. You should assess the quality of generated
//  signal if you want to emit a noticeable power.
//      This is an experimental project of amateur radio class and it is devised
//  by me on the free will base in order to experiment with QRP narrowband
//  digital modes.
//      I appreciate any thoughts or comments on that matter.
//
//  PLATFORM
//      Raspberry Pi pico.
//
//  REVISION HISTORY
// 
//      Rev 0.1   05 Nov 2023   Initial release
//      Rev 0.2   18 Nov 2023
//      Rev 1.0   10 Dec 2023   Improved frequency range (to ~33.333 MHz).
//
//  PROJECT PAGE
//      https://github.com/RPiks/pico-hf-oscillator
//
//  LICENCE
//      MIT License (http://www.opensource.org/licenses/mit-license.php)
//
//  Copyright (c) 2023 by Roman Piksaykin
//  
//  Permission is hereby granted, free of charge,to any person obtaining a copy
//  of this software and associated documentation files (the Software), to deal
//  in the Software without restriction,including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY,WHETHER IN AN ACTION OF CONTRACT,TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.
///////////////////////////////////////////////////////////////////////////////
#include "piodco.h"

#include <string.h>
#include "../lib/assert.h"

#include "dco2.pio.h"
#if WSPR_DCO_FINE
#include "dco3.pio.h"
#endif

volatile int32_t si32precise_cycles;

#if WSPR_DCO_FINE
/* "Fine" engine state (N2GF 10 m port). dco_reg4 = (cycles per 4 half periods - 8) * 2^dco_shift. */
volatile uint32_t dco_reg4;
volatile uint32_t dco_shift;
/* Extra cycle distribution for 0..3 leftover cycles over the 4 half periods of one word. */
static uint32_t RAM_A dco_spread[4] = {0x00000000u, 0x00000001u, 0x00010001u, 0x00010101u};
#endif

/// @brief Highest output frequency the DCO can make at this system clock, Hz.
/// @brief Classic engine: half period >= PIOASM_DELAY_CYCLES (4) -> sysclk / 8.
/// @brief Fine engine: limited by the core1 worker (>= 16 cycles per FIFO word) -> sysclk / 8.
uint32_t PioDCOMaxFreqHz(void)
{
    return (uint32_t)(PLL_SYS_MHZ * MHz / 8UL);
}

/// @brief Initializes DCO context and prepares PIO hardware.
/// @param pdco Ptr to DCO context.
/// @param gpio The GPIO of DCO output.
/// @param cpuclkhz The system CPU clock freq., Hz.
/// @return 0 if OK.
int PioDCOInit(PioDco *pdco, int gpio)
{
    assert_(pdco);
  
    /* N2GF: keep the GPS context pointer; main() may already have set it from core0. */
    GPStimeContext *pgps = pdco->_pGPStime;
    memset(pdco, 0, sizeof(PioDco));
    pdco->_pGPStime = pgps;

    //pdco->_clkfreq_hz = cpuclkhz;
    pdco->_pio = pio0;
    pdco->_gpio = gpio;
#if WSPR_DCO_FINE
    pdco->_offset = pio_add_program(pdco->_pio, &dco3_program);
    pdco->_ism = pio_claim_unused_sm(pdco->_pio, true);
    gpio_init(pdco->_gpio);
    dco3_program_init(pdco->_pio, pdco->_ism, pdco->_offset, pdco->_gpio);
#else
    pdco->_offset = pio_add_program(pdco->_pio, &dco_program);
    pdco->_ism = pio_claim_unused_sm(pdco->_pio, true);

    gpio_init(pdco->_gpio);
    pio_gpio_init(pdco->_pio, pdco->_gpio);

    dco_program_init(pdco->_pio, pdco->_ism, pdco->_offset, pdco->_gpio);
    pdco->_pio_sm = dco_program_get_default_config(pdco->_offset);

    sm_config_set_out_shift(&pdco->_pio_sm, true, true, 32);           // Autopull.
    sm_config_set_fifo_join(&pdco->_pio_sm, PIO_FIFO_JOIN_TX);
    sm_config_set_set_pins(&pdco->_pio_sm, pdco->_gpio, 1);
    
    pio_sm_init(pdco->_pio, pdco->_ism, pdco->_offset, &pdco->_pio_sm);
#endif

    return 0;
}

/// @brief Sets DCO working frequency in Hz: Fout = ui32_frq_hz + ui32_frq_millihz * 1e-3.
/// @param pdco Ptr to DCO context.
/// @param i32_frq_hz The `coarse` part of frequency [Hz]. Might be negative.
/// @param ui32_frq_millihz The `fine` part of frequency [Hz].
/// @return 0 if OK. -1 invalid freq.
/// @attention The func can be called while DCO running.
int PioDCOSetFreq(PioDco *pdco, uint32_t ui32_frq_hz, int32_t ui32_frq_millihz)
{
    assert_(pdco);

    /* RPix: Calculate an accurate value of phase increment of the freq 
       per 1 tick of CPU clock, here 2^24 is scaling coefficient. */
    const int64_t i64denominator = 2000LL * (int64_t)ui32_frq_hz + (int64_t)ui32_frq_millihz;
    if(i64denominator <= 0)
    {
        return -1;
    }

#if WSPR_DCO_FINE
    {
        /* Cycles for 4 half periods = 2 * sysclk / f_eff = 4000 * sysclk / den, with
           f_eff = f + millihz / 2000 (same semantics as the classic engine).
           Two-stage division keeps every intermediate below 2^63. */
        const uint64_t A   = (uint64_t)(PLL_SYS_MHZ * MHz) * 4000ULL;
        const uint64_t den = (uint64_t)i64denominator;
        const uint64_t q20 = (A << 20) / den;
        const uint64_t r20 = (A << 20) % den;
        const uint64_t c4  = q20 >> 20;          /* integer cycles per 4 half periods */

        /* >= 16 cycles per word keeps the core1 worker ahead of the PIO (f <= sysclk/8);
           <= 1023 + 8 keeps each 8-bit half-period count in range. */
        if(c4 < 16 || c4 > 1029)
        {
            return -1;
        }

        /* Pick the scale: largest S (20..27) with (c4 - 8 + 2) << S < 2^31.  Keep the
           current S while it still fits and is within 2 bits of optimum, so WSPR tone
           changes never change the scale while the worker runs. */
        uint32_t S = dco_shift;
        const uint64_t need = c4 - 8 + 2;
        if(S < 20 || S > 27 || (need << S) >= (1ULL << 31) || (need << (S + 2)) < (1ULL << 31))
        {
            S = 27;
            while(S > 20 && (need << S) >= (1ULL << 31))
            {
                --S;
            }
        }

        const uint64_t val = (q20 << (S - 20)) + (((r20 << (S - 20)) + (den >> 1)) / den);
        const uint32_t reg = (uint32_t)(val - (8ULL << S));

        if(S != dco_shift)
        {
            dco_reg4 = reg;     /* worker resets its error accumulator on a scale change */
            dco_shift = S;
        }
        dco_reg4 = reg;

        pdco->_frq_cycles_per_pi = (int32_t)(val >> (S - 20));   /* informational: c4 * 2^20 */
        pdco->_ui32_frq_hz = ui32_frq_hz;
        pdco->_ui32_frq_millihz = ui32_frq_millihz;
        return 0;
    }
#else
    const int32_t frq_cycles_per_pi = (int32_t)(((int64_t)(PLL_SYS_MHZ * MHz) * (int64_t)(1<<24) * 1000LL
                                         +(i64denominator>>1)) / i64denominator);

    /* N2GF: above sysclk/8 the half period would be < PIOASM_DELAY_CYCLES and
       si32precise_cycles negative: the worker then emits y = 255 bursts and the
       output collapses to a few hundred kHz of garbage.  Refuse instead. */
    if(frq_cycles_per_pi < (PIOASM_DELAY_CYCLES << 24))
    {
        return -1;
    }

    pdco->_frq_cycles_per_pi = frq_cycles_per_pi;
    si32precise_cycles = pdco->_frq_cycles_per_pi - (PIOASM_DELAY_CYCLES<<24);

    pdco->_ui32_frq_hz = ui32_frq_hz;
    pdco->_ui32_frq_millihz = ui32_frq_millihz;

    return 0;
#endif
}

/// @brief Obtains the frequency shift [milliHz] which is calculated for a given frequency.
/// @param pdco Ptr to Context.
/// @param u64_desired_frq_millihz The frequency for which we want to calculate correction.
/// @return The value of correction we need to subtract from desired freq. to compensate
/// @return Pico's reference clock shift. 2854974.
int32_t PioDCOGetFreqShiftMilliHertz(const PioDco *pdco, uint64_t u64_desired_frq_millihz)
{
    assert_(pdco);
    if(!pdco->_pGPStime)
    {
        return 0U;
    }

    static int64_t i64_last_correction = 0;
    const int64_t dt = pdco->_pGPStime->_time_data._i32_freq_shift_ppb; /* Parts per billion. */
    if(dt)
    {
        i64_last_correction = dt;
    }

    int32_t i32ret_millis;
    if(i64_last_correction)
    {
        int64_t i64corr_coeff = (u64_desired_frq_millihz + 500000LL) / 1000000LL;
        i32ret_millis = (i64_last_correction * i64corr_coeff + 50000LL) / 1000000LL;

        return i32ret_millis;
    }

    return 0U;
}

/// @brief Starts the DCO.
/// @param pdco Ptr to DCO context.
void PioDCOStart(PioDco *pdco)
{
    assert_(pdco);
    /* N2GF: clear the sticky "TX FIFO ran dry" flag so PioDCOUnderrun() reports this Tx only. */
    pdco->_pio->fdebug = (1u << (PIO_FDEBUG_TXSTALL_LSB + pdco->_ism));
    pio_sm_set_enabled(pdco->_pio, pdco->_ism, true);

    pdco->_is_enabled = YES;
}

/// @brief Stops the DCO.
/// @param pdco Ptr to DCO context.
void PioDCOStop(PioDco *pdco)
{
    assert_(pdco);
    pio_sm_set_enabled(pdco->_pio, pdco->_ism, false);

    pdco->_is_enabled = NO;
}

/// @brief Main worker task of DCO V.2. It is time critical, so it ought to be run on
/// @brief the dedicated pi pico core.
/// @param pDCO Ptr to DCO context.
/// @return No return. It spins forever.
void RAM (PioDCOWorker2)(PioDco *pDCO)
{
    register PIO pio = pDCO->_pio;
    register uint sm = pDCO->_ism;
    register int32_t i32acc_error = 0;
    register uint32_t i32wc, i32reg;
    
LOOP:
    i32reg = si32precise_cycles;
    i32wc = (i32reg - i32acc_error) >> 24U;
    pio_sm_put_blocking(pio, sm, i32wc);
    i32acc_error += (i32wc << 24U) - i32reg;
    
    goto LOOP;
}

#if WSPR_DCO_FINE
/// @brief N2GF "fine" worker for dco3.pio. One FIFO word = 4 half periods with
/// @brief individual 8-bit counts. The total for the 4 halves is dithered with a
/// @brief first-order accumulator at 2^dco_shift resolution and the 0..3 leftover
/// @brief cycles are spread over the halves, so phase error stays below ~1 cycle.
/// @brief Unrolled x4 with one FIFO-level check per 4 words to stay ahead of the PIO
/// @brief (budget at 28.126 MHz / 288 MHz sysclk: 20.5 cycles per word).
void RAM (PioDCOWorker3)(PioDco *pDCO)
{
    PIO pio = pDCO->_pio;
    const uint sm = pDCO->_ism;
    io_wo_32 *txf = &pio->txf[sm];
    const io_ro_32 *flevel = &pio->flevel;
    const uint32_t lvl_lsb = sm * 8u;           /* TXn level, 4 bits (joined FIFO: 0..8) */
    const uint32_t *spread = dco_spread;
    uint32_t S = dco_shift;
    int32_t err = 0;

    for(;;)
    {
        const uint32_t reg = dco_reg4;
        const uint32_t s = dco_shift;
        if(s != S)
        {
            S = s;
            err = 0;
        }

        while(((*flevel >> lvl_lsb) & 0xFu) > 4u)
        {
            /* wait for 4 free slots */
        }

#define DCO3_WORD()                                                         \
        {                                                                   \
            const uint32_t w = (reg - (uint32_t)err) >> S;                  \
            err += (int32_t)((w << S) - reg);                               \
            *txf = (w >> 2) * 0x01010101u + spread[w & 3u];                 \
        }
        DCO3_WORD();
        DCO3_WORD();
        DCO3_WORD();
        DCO3_WORD();
#undef DCO3_WORD
    }
}
#endif

/// @brief True if the PIO ran out of data (core1 worker too slow) since PioDCOStart().
int PioDCOUnderrun(const PioDco *pdco)
{
    return (pdco->_pio->fdebug >> (PIO_FDEBUG_TXSTALL_LSB + pdco->_ism)) & 1u;
}

/// @brief Main worker task of DCO. It is time critical, so it ought to be run on
/// @brief the dedicated pi pico core.
/// @param pDCO Ptr to DCO context.
/// @return No return. It spins forever.
void RAM (PioDCOWorker)(PioDco *pDCO)
{
    assert_(pDCO);

    register PIO pio = pDCO->_pio;
    register uint sm = pDCO->_ism;
    register int32_t i32acc_error = 0;
    register uint32_t *preg32 = pDCO->_ui32_pioreg;
    register uint8_t *pu8reg = (uint8_t *)preg32;

    for(;;)
    {
        const register int32_t i32reg = si32precise_cycles;
        /* RPix: Load the next precise value of CPU CLK cycles per DCO cycle,
           scaled by 2^24. It yields about 24 millihertz resolution at @10MHz
           DCO frequency. */
        for(int i = 0; i < 32; ++i)
        {
            /* RPix: Calculate the integer number of CPU CLK cycles per next
               DCO cycle, corrected by accumulated error (feedback of the PLL). */
            const int32_t i32wc = iSAR32(i32reg - i32acc_error + (1<<23), 24);

            /* RPix: Calculate the difference betwixt calculated value scaled to
               fine resolution back and precise value of DCO cycles per CPU CLK cycle. 
               This forms a phase locked loop which provides precise freq */
            i32acc_error += (i32wc<<24) - i32reg;

            /* RPix: Set PIO array contents corrected by pio program delay
               of N CPU CLK cycles owing to pio asm instructions. */
            pu8reg[i] = i32wc - PIOASM_DELAY_CYCLES;
        }

        dco_program_puts(pio, sm, preg32);
    }
}

/// @brief Sets DCO running mode.
/// @param pdco Ptr to DCO context.
/// @param emode Desired mode.
/// @attention Not actual so far. User-independent freq. correction not impl'd yet. !FIXME!
void PioDCOSetMode(PioDco *pdco, enum PioDcoMode emode)
{
    assert_(pdco);
    pdco->_mode = emode;

    switch(emode)
    {
        case eDCOMODE_IDLE:
            PioDCOStop(pdco);
            break;

        case eDCOMODE_GPS_COMPENSATED:
            PioDCOStart(pdco);
            break;

        default:
            break;
    }
}
