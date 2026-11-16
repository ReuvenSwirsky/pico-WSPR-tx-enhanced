///////////////////////////////////////////////////////////////////////////////
//
//  Roman Piksaykin [piksaykin@gmail.com], R2BDY
//  https://www.qrz.com/db/r2bdy
//
///////////////////////////////////////////////////////////////////////////////
//
//
//  init.c - Pico hardware initializations.
// 
//  DESCRIPTION
//      The pico-WSPR-tx project provides WSPR beacon function using only
//  Pi Pico board. *NO* additional hardware such as freq.synth required.
//
//  HOWTOSTART
//  .
//
//  PLATFORM
//      Raspberry Pi pico.
//
//  REVISION HISTORY
// 
//      Rev 0.1   18 Nov 2023
//  Initial release.
//
//  PROJECT PAGE
//      https://github.com/RPiks/pico-WSPR-tx
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
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "defines.h"


#if PICO_RP2350
#include "hardware/structs/qmi.h"
#include "hardware/structs/io_qspi.h"
#include "hardware/sync.h"

/// @brief N2GF: keep the QSPI flash clock in spec when clk_sys is overclocked.
/// @brief The RP2350 bootrom leaves XIP at clk_sys/3 (50 MHz at 150 MHz); at 288 MHz
/// @brief that would be 96 MHz with an unknown sample delay. We use SCK <= 75 MHz and
/// @brief sample half an SCK period late (RXDELAY = CLKDIV half-cycles, the same rule
/// @brief MicroPython uses for RP2350 overclocking). Runs from RAM, IRQs off.
/// @brief Must also be called after every flash erase/program (the bootrom's XIP
/// @brief restore puts CLKDIV back to 3) - see settingsWriteToFlash().
void __no_inline_not_in_flash_func(N2GF_FlashTimingApply)(void)
{
#if defined(WSPR_SYSCLK_MHZ)
    if(PLL_SYS_MHZ <= 150)
    {
        return;
    }
    uint32_t div = (PLL_SYS_MHZ + 74UL) / 75UL;          /* SCK <= 75 MHz */
    if(div < 2) div = 2;
    uint32_t rxdelay = div > 7 ? 7 : div;

    const uint32_t irq = save_and_disable_interrupts();
    /* Wait until flash CS is deselected (QMI has no busy flag). */
    while((io_qspi_hw->io[1].status & IO_QSPI_GPIO_QSPI_SS_STATUS_OUTTOPAD_BITS) !=
          IO_QSPI_GPIO_QSPI_SS_STATUS_OUTTOPAD_BITS)
    {
    }
    uint32_t t = qmi_hw->m[0].timing;
    t &= ~(QMI_M0_TIMING_CLKDIV_BITS | QMI_M0_TIMING_RXDELAY_BITS);
    t |= (div << QMI_M0_TIMING_CLKDIV_LSB) | (rxdelay << QMI_M0_TIMING_RXDELAY_LSB);
    qmi_hw->m[0].timing = t;
    /* Uncached XIP read so the new timing is used before we return to flash code. */
    (void)*(volatile uint32_t *)XIP_NOCACHE_NOALLOC_BASE;
    restore_interrupts(irq);
#endif
}
#else
void N2GF_FlashTimingApply(void)
{
}
#endif

/// @brief Initializes Pi pico low level hardware.
void InitPicoHW(void)
{
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

#if PICO_RP2350 && defined(WSPR_SYSCLK_MHZ)
    /* N2GF: RP2350 overclock. Raise the core voltage first, slow the flash, then
       switch clk_sys. clk_peri stays on the 48 MHz USB PLL (set_sys_clock_khz()
       does that), because RP2350 specifies clk_peri <= 150 MHz. */
    if(PLL_SYS_MHZ > 150)
    {
        enum vreg_voltage v = PLL_SYS_MHZ > 250 ? VREG_VOLTAGE_1_30 :
                              PLL_SYS_MHZ > 200 ? VREG_VOLTAGE_1_25 : VREG_VOLTAGE_1_15;
        vreg_set_voltage(v);
        sleep_ms(10);
        N2GF_FlashTimingApply();
    }
    set_sys_clock_khz(PLL_SYS_MHZ * 1000UL, true);
#else
    if (PLL_SYS_MHZ > 125)
    {
        vreg_set_voltage(VREG_VOLTAGE_1_20);
        sleep_ms(10);
    }

    const uint32_t clkhz = PLL_SYS_MHZ * 1000000L;
    set_sys_clock_khz(clkhz / kHz, true);

    clock_configure(clk_peri, 0,
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                    PLL_SYS_MHZ * MHZ,
                    PLL_SYS_MHZ * MHZ);
#endif
}
