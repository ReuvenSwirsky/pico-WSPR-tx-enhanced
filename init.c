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
#if PICO_RP2350
#include "hardware/structs/qmi.h"
#endif
#include "defines.h"

#if PICO_RP2350
/// @brief Sets the QSPI flash clock divider (flash clock = clk_sys / clkdiv).
/// Must run from RAM because it changes the timing of the XIP interface we
/// would otherwise be executing from.
static void __no_inline_not_in_flash_func(SetFlashClkDiv)(uint32_t clkdiv)
{
    hw_write_masked(&qmi_hw->m[0].timing,
                    clkdiv << QMI_M0_TIMING_CLKDIV_LSB,
                    QMI_M0_TIMING_CLKDIV_BITS);
}
#endif

/// @brief Initializes Pi pico low level hardware.
void InitPicoHW(void)
{
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

#if PICO_RP2350
    /* The Pico 2 board file uses flash clkdiv 2, which would clock the W25Q32
       at 150 MHz when clk_sys is 300 MHz (its limit is 133 MHz). Divide by 3
       -> 100 MHz. Done before raising clk_sys so the flash never runs out of spec. */
    SetFlashClkDiv(3);
#endif

    if (PLL_SYS_MHZ > 125)
    {
        vreg_set_voltage(VREG_VOLTAGE_1_20);
        sleep_ms(10);
    }

    const uint32_t clkhz = PLL_SYS_MHZ * 1000000L;
    set_sys_clock_khz(clkhz / kHz, true);

#if !PICO_RP2350
    clock_configure(clk_peri, 0,
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                    PLL_SYS_MHZ * MHZ,
                    PLL_SYS_MHZ * MHZ);
#endif
    /* On RP2350 clk_peri is left where set_sys_clock_khz() puts it (48 MHz from
       the USB PLL) rather than running the UART from a 300 MHz peripheral clock.
       The PIO DCO runs from clk_sys and is unaffected. */
}
