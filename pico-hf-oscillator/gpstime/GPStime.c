///////////////////////////////////////////////////////////////////////////////
//
//  Roman Piksaykin [piksaykin@gmail.com], R2BDY, PhD
//  https://www.qrz.com/db/r2bdy
//
///////////////////////////////////////////////////////////////////////////////
//
//
//  gpstime.c - GPS time reference utilities for digital controlled radio freq
//              oscillator based on Raspberry Pi Pico.
//
//  DESCRIPTION
//
//      GPS time utilities for pico-hf-oscillator calculate a precise frequency
//  shift between the local Pico oscillator and reference oscill. of GPS system.
//  The value of the shift is used to correct generated frequency. The practical 
//  precision of this solution depends on GPS receiver's time pulse stability, 
//  as well as on quality of navigation solution of GPS receiver. 
//  This quality can be estimated by GDOP and TDOP parameters received 
//  in NMEA-0183 message packet from GPS receiver.
//      Owing to the meager frequency step in millihertz range, we obtain
//  a quasi-analog precision frequency source (if the GPS navigation works OK).
//      This is an experimental project of amateur radio class and it is devised
//  by me on the free will base in order to experiment with QRP narrowband
//  digital modes including extremely ones such as QRSS.
//      I appreciate any thoughts or comments on that matter.
//
//  PLATFORM
//      Raspberry Pi pico.
//
//  REVISION HISTORY
// 
//      Rev 0.1   25 Nov 2023   Initial release
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
#include "GPStime.h"

GPStimeContext gTimeContext = {0};
volatile static GPStimeData *spGPStimeData = NULL;

/// @brief Initializes GPS time module Context.
/// @param uart_id UART id to which GPS receiver is connected, 0 OR 1.
/// @param uart_baud UART baudrate, 115200 max.
/// @param pps_gpio GPIO pin of PPS (second pulse) from GPS receiver.
/// @return the new GPS time Context.
GPStimeContext *GPStimeInit(int uart_id, int uart_baud, int pps_gpio)
{
    ASSERT_(0 == uart_id || 1 == uart_id);
    ASSERT_(uart_baud <= 115200);
    ASSERT_(pps_gpio < 29);

    // Set up our UART with the required speed & assign pins.
    uart_init(uart_id ? uart1 : uart0, uart_baud);
    gpio_set_function(uart_id ? 8 : 0, GPIO_FUNC_UART);
    gpio_set_function(uart_id ? 9 : 1, GPIO_FUNC_UART);

    GPStimeContext *pgt = &gTimeContext;
  
    gTimeContext._uart_id = uart_id;
    gTimeContext._uart_baudrate = uart_baud;
    gTimeContext._pps_gpio = pps_gpio;
    gTimeContext.GpsNmeaReceived = false;


    spGPStimeData = &gTimeContext._time_data;

    gpio_init(pps_gpio);
    gpio_set_dir(pps_gpio, GPIO_IN);
    PPSCalReset(&gPPSCal);
    gpio_set_irq_enabled_with_callback(pps_gpio, GPIO_IRQ_EDGE_RISE, true, &GPStimePPScallback);
    irq_set_priority(IO_IRQ_BANK0, 0x40);   /* N2GF: PPS timestamp ahead of UART/alarm ISRs (less jitter) */

    uart_set_hw_flow(uart_id ? uart1 : uart0, false, false);
    uart_set_format(uart_id ? uart1 : uart0, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(uart_id ? uart1 : uart0, false);
    irq_set_exclusive_handler(uart_id ? UART1_IRQ : UART0_IRQ, GPStimeUartRxIsr);
    irq_set_enabled(uart_id ? UART1_IRQ : UART0_IRQ, true);
    uart_set_irq_enables(uart_id ? uart1 : uart0, true, false);

    return pgt;
}



volatile bool ppsTriggered = false;

/* N2GF: PPS edge filter. After an accepted edge the PPS GPIO interrupt is
   disabled and re-armed 900 ms later from a timer alarm, so RF picked up on
   the PPS line (or a noisy receiver) can never flood core0 with interrupts.
   The time of each accepted edge is kept for the scheduler. */
#define N2GF_PPS_REARM_US 900000
volatile uint64_t gu64_pps_time_us = 0;      /* time_us_64() at last accepted PPS edge */
volatile uint32_t gu32_pps_count = 0;        /* accepted PPS edges */
volatile uint32_t gu32_pps_rejected = 0;     /* edges ignored by the filter */
PPSCal gPPSCal;                              /* N2GF: safe PPS frequency calibration */

static int64_t GPStimePPSRearm(alarm_id_t id, void *user_data)
{
    (void)id; (void)user_data;
    const uint gpio = (uint)gTimeContext._pps_gpio;
    gpio_acknowledge_irq(gpio, GPIO_IRQ_EDGE_RISE);      /* drop edges seen while disarmed */
    gpio_set_irq_enabled(gpio, GPIO_IRQ_EDGE_RISE, true);
    return 0;
}

/// @brief The PPS interrupt service subroutine.
/// @param  gpio The GPIO pin of Pico which is connected to PPS output of GPS rec.
void RAM (GPStimePPScallback)(uint gpio, uint32_t events)
{   
    const uint64_t now = time_us_64();
    if(gu32_pps_count && (now - gu64_pps_time_us) < N2GF_PPS_REARM_US)
    {
        ++gu32_pps_rejected;
        return;
    }
    gu64_pps_time_us = now;
    ++gu32_pps_count;
    if(spGPStimeData)
    {
        spGPStimeData->_u64_sysclk_pps_last = now;
    }
    ppsTriggered = true;// used by the foreground loop

    /* N2GF: safe replacement for the FIX_BUGS_IN_THIS block below (signed 64-bit,
       full 32-interval windows only, +-100 ppm sanity, IIR + outlier reject, clamp). */
    PPSCalOnEdge(&gPPSCal, now);
    if(spGPStimeData)
    {
        spGPStimeData->_i32_freq_shift_ppb = gPPSCal.ppb;   /* for the status dumps only */
    }

    gpio_set_irq_enabled(gpio, GPIO_IRQ_EDGE_RISE, false);
    if(add_alarm_in_us(N2GF_PPS_REARM_US, GPStimePPSRearm, NULL, true) <= 0)
    {
        gpio_set_irq_enabled(gpio, GPIO_IRQ_EDGE_RISE, true);   /* no alarm slot: stay armed */
    }

#ifdef FIX_BUGS_IN_THIS    
// VK3KYY
// This functionality seems to have bugs in it which cause it to return huge positive numbers
// So I have currently disabled it via the ifdef
// N2GF: root cause = "_u64_pps_period_1M (uint64) - (int64)nominal" is evaluated unsigned, so a
// slow crystal (period < nominal) wraps to 2^64/32 = 5.76e17 "ppb". Kept for reference only;
// replaced by PPSCalOnEdge() above (ppscal.c). Do not enable.

    if(spGPStimeData)
    {    
        const uint64_t tm64 = GetUptime64();
        spGPStimeData->_u64_sysclk_pps_last = tm64;   
        ++spGPStimeData->_ix_last;
        spGPStimeData->_ix_last %= eSlidingLen;

        const int64_t dt_per_window = tm64 - spGPStimeData->_pu64_sliding_pps_tm[spGPStimeData->_ix_last];
        spGPStimeData->_pu64_sliding_pps_tm[spGPStimeData->_ix_last] = tm64;

        if(ABS(dt_per_window - eCLKperTimeMark * eSlidingLen) < eMaxCLKdevPPM * eSlidingLen)
        {
            if(spGPStimeData->_u64_pps_period_1M)
            {
                spGPStimeData->_u64_pps_period_1M += iSAR64((int64_t)eDtUpscale * dt_per_window 
                                                            - spGPStimeData->_u64_pps_period_1M + 2, 2);
                spGPStimeData->_i32_freq_shift_ppb = (spGPStimeData->_u64_pps_period_1M
                                                      - (int64_t)eDtUpscale * eCLKperTimeMark * eSlidingLen
                                                      + (eSlidingLen >> 1)) / eSlidingLen;
            }
            else
            {
                spGPStimeData->_u64_pps_period_1M = (int64_t)eDtUpscale * dt_per_window;
            }
        }
    }
#endif    
}

/// @brief N2GF: UTC unix time at instant t_us (time_us_64() timebase).
/// @brief Anchored on the PPS edge that preceded the last valid RMC sentence
/// @brief (that sentence reports the second that PPS edge started), so the
/// @brief result stays right if PPS pulses go missing (e.g. GPS desensed by our
/// @brief own transmitter) - it free-runs on the crystal timer (~+-30 ppm).
/// @return 0 if OK, -1 no fix yet, -2 fix older than 24 h.
int GPStimeGetTimeAtUs(uint64_t t_us, uint32_t *u32_tmdst)
{
    const uint32_t t0 = gTimeContext._time_data._u32_utime_nmea_last;
    const uint64_t a0 = gTimeContext._time_data._u64_pps_of_nmea_last;
    if(!t0 || !a0)
    {
        return -1;
    }
    if(t_us + 500000ULL < a0)
    {
        return -1;
    }
    const uint64_t dt = t_us + 500000ULL - a0;          /* round to nearest second */
    if(dt > 86400ULL * 1000000ULL)
    {
        return -2;
    }
    *u32_tmdst = t0 + (uint32_t)(dt / 1000000ULL);
    return 0;
}

/// @brief Calculates current unixtime using data available.
/// @param pg Ptr to the context.
/// @param u32_tmdst Ptr to destination unixtime val.
/// @return 0 if OK.
/// @return -1 There was NO historical GPS fixes.
/// @return -2 The fix was expired (24hrs or more time ago).
int GPStimeGetTime(uint32_t *u32_tmdst)
{
    assert(u32_tmdst);

    /* If there has been no fix, it's no way to get any time data... */
    if(!gTimeContext._time_data._u32_utime_nmea_last)
    {
        return -1;
    }

    const uint64_t tm64 = GetUptime64();
    const uint64_t dt = tm64 - gTimeContext._time_data._u64_sysclk_nmea_last;
    const uint32_t dt_sec = PicoU64timeToSeconds(dt);

    /* If expired. */
    if(dt_sec > 86400)
    {
        return -2;
    }

    *u32_tmdst = gTimeContext._time_data._u32_utime_nmea_last + dt_sec;

    return 0;
}

/// @brief UART FIFO ISR. Processes another N chars receiver from GPS rec.
void RAM (GPStimeUartRxIsr)()
{
    
    {
        uart_inst_t *puart_id = gTimeContext._uart_id ? uart1 : uart0;
        for(;;uart_is_readable(puart_id))
        {
            uint8_t chr = uart_getc(puart_id);
            gTimeContext._pbytebuff[gTimeContext._u8_ixw++] = chr;
            gTimeContext._is_sentence_ready = ('\n' == chr);
            break;
        }

        if(gTimeContext._is_sentence_ready)
        {
            gTimeContext.GpsNmeaReceived = true;
            /* N2GF: terminate the sentence so stale bytes from a longer, older
               sentence are never parsed (buffer is 256 bytes, index is uint8_t). */
            if(gTimeContext._u8_ixw)
            {
                gTimeContext._pbytebuff[gTimeContext._u8_ixw] = 0;
            }
            gTimeContext._u8_ixw = 0;
            gTimeContext._i32_error_count -= GPStimeProcNMEAsentence();
        }
    }
}

/// @brief Processes a NMEA sentence GPRMC.
/// @param pg Ptr to Context.
/// @return 0 OK.
/// @return -2 Error: bad lat format.
/// @return -3 Error: bad lon format.
/// @return -4 Error: no final '*' char ere checksum value.
/// @attention Checksum validation is not implemented so far. !FIXME!
int GPStimeProcNMEAsentence(void)
{
    uint8_t *prmc = (uint8_t *)strnstr((char *)gTimeContext._pbytebuff, "$GPRMC,", sizeof(gTimeContext._pbytebuff));

    if (prmc == NULL)
    {
        prmc = (uint8_t *)strnstr((char *)gTimeContext._pbytebuff, "$GNRMC,", sizeof(gTimeContext._pbytebuff));
    }

    if(prmc)
    {
        /* N2GF: (1) verify the NMEA checksum - a sentence corrupted by RF pickup on
           the UART line could otherwise set a wrong time or position;
           (2) field offsets are now relative to prmc (they were relative to the
           buffer start but used relative to prmc);
           (3) longitude sign used the N/S field instead of E/W, so West longitudes
           came out East and the GPS-derived locator was wrong in the Americas. */
        const uint8_t *pend = gTimeContext._pbytebuff + sizeof(gTimeContext._pbytebuff);
        const uint8_t *pstar = prmc + 1;
        uint8_t chk = 0;
        while(pstar < pend && *pstar && *pstar != '*')
        {
            chk ^= *pstar++;
        }
        if(pstar + 2 >= pend || *pstar != '*')
        {
            ++gTimeContext._time_data._u32_nmea_bad_count;
            return -4;
        }
        {
            char hx[3] = { (char)pstar[1], (char)pstar[2], 0 };
            char *pe = NULL;
            const long want = strtol(hx, &pe, 16);
            if(pe != hx + 2 || want != chk)
            {
                ++gTimeContext._time_data._u32_nmea_bad_count;
                return -5;
            }
        }

        ++gTimeContext._time_data._u32_nmea_gprmc_count;

        uint64_t tm_fix = GetUptime64();
        const uint64_t pps_at_fix = gu64_pps_time_us;
        uint32_t u8ixcollector[32] = {0};
        uint32_t paramaterCount = 0;
        const uint32_t len = (uint32_t)(pstar - prmc);

        for(uint32_t index = 0; index < len && paramaterCount < 32; ++index)
        {
            if(',' == prmc[index])
            {
                prmc[index] = 0;
                u8ixcollector[paramaterCount++] = index + 1;
            }
        }
        prmc[len] = 0;   /* terminate the last field at '*' */

        if(paramaterCount < 9)
        {
            return -6;
        }

        gTimeContext._time_data._u8_is_solution_active = 'A' == prmc[u8ixcollector[1]];

        if(gTimeContext._time_data._u8_is_solution_active)
        {
            double tmp = atof((const char *)prmc + u8ixcollector[2]);
 
            int intPart = (int)tmp;
            double mins = (intPart % 100);
            int degs = intPart / 100;
            double degMins = tmp - intPart;

            mins = mins + degMins;
            gTimeContext._time_data.lat = (double) degs + mins/60.f;
            
            if('S' == prmc[u8ixcollector[3]]) 
            {
                gTimeContext._time_data.lat *= -1;// South are negative deg
            }

            tmp = atof((const char *)prmc + u8ixcollector[4]);

            intPart = (int)tmp;
            mins = (intPart % 100);
            degs = intPart / 100;
            degMins = tmp - intPart;

            mins = mins + degMins;
            gTimeContext._time_data.lon = (double) degs + mins/60.f;
            
            if('W' == prmc[u8ixcollector[5]]) 
            {
                gTimeContext._time_data.lon *= -1;// West are negative deg
            }

            snprintf((char *)gTimeContext._time_data.lastRMCDateTime, sizeof(gTimeContext._time_data.lastRMCDateTime),
                     "%.8s %.10s", (char *)(prmc + u8ixcollector[8]), (char *)(prmc + u8ixcollector[0]));

            const uint32_t ut = GPStime2UNIX((const char *)prmc + u8ixcollector[8], (const char *)prmc + u8ixcollector[0]);
            if(ut)
            {
                gTimeContext._time_data._u32_utime_nmea_last = ut;
                gTimeContext._time_data._u64_sysclk_nmea_last = tm_fix;
                /* The RMC reports the second that began at the PPS edge before it.
                   Use that edge if it is recent (< 1.2 s), else estimate from the
                   sentence arrival time (RMC usually arrives 0.1..0.9 s after PPS). */
                gTimeContext._time_data._u64_pps_of_nmea_last =
                    (pps_at_fix && (tm_fix - pps_at_fix) < 1200000ULL) ? pps_at_fix
                                                                         : (tm_fix > 500000ULL ? tm_fix - 500000ULL : 1);
            }
        }
    }
    
    return 0;
}

/// @brief Converts GPS time and date strings to unix time.
/// @param pdate Date string, 6 chars in work.
/// @param ptime Time string, 6 chars in work.
/// @return Unix timestamp (epoch). 0 if bad imput format.
uint32_t GPStime2UNIX(const char *pdate, const char *ptime)
{
    assert_(pdate);
    assert_(ptime);

    if(strlen(pdate) == 6 && strlen(ptime) > 5)
    {
        struct tm ltm = {0};

        ltm.tm_year = 100 + DecimalStr2ToNumber(pdate + 4);
        ltm.tm_mon  = DecimalStr2ToNumber(pdate + 2) - 1;
        ltm.tm_mday = DecimalStr2ToNumber(pdate);

        ltm.tm_hour = DecimalStr2ToNumber(ptime);
        ltm.tm_min = DecimalStr2ToNumber(ptime + 2);
        ltm.tm_sec = DecimalStr2ToNumber(ptime + 4);

        return mktime(&ltm);
    }

    return 0;
}

/// @brief Dumps the GPS data struct to stdio.
/// @param pd Ptr to Context.
void GPStimeDump(const GPStimeData *pd)
{
    assert_(pd);

    printf("\nGPS solution is active:%u\n", pd->_u8_is_solution_active);
    printf("GPRMC count:%lu\n", pd->_u32_nmea_gprmc_count);
    printf("NMEA unixtime last:%lu\n", pd->_u32_utime_nmea_last);
    printf("NMEA sysclock last:%llu\n", pd->_u64_sysclk_nmea_last);
    printf("GPS Latitude:%d Longtitude:%d\n", pd->lat, pd->lon);
    printf("PPS sysclock last:%llu\n", pd->_u64_sysclk_pps_last);
    printf("PPS period *1e6:%llu\n", (pd->_u64_pps_period_1M + (eSlidingLen>>1)) / eSlidingLen);
    printf("FRQ correction ppb:%lld\n\n", pd->_i32_freq_shift_ppb);
}
