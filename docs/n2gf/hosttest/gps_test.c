/* Runs the REAL pico-hf-oscillator/gpstime/GPStime.c (N2GF version) on the host with stubs.
 * Tests: NMEA checksum, field parsing, West longitude sign, PPS edge filter, and
 * GPStimeGetTimeAtUs() holdover when PPS stops.                                   */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "GPStime.c"   /* the real firmware source, single TU */
uint64_t stub_now_us; int stub_irq_enabled, stub_alarm_pending; alarm_callback_t stub_alarm_cb;
struct stub_timer stub_timer_hw; uart_inst_t stub_uart0, stub_uart1; const char *stub_rx = "";
void assert_(bool v) { if (!v) { printf("assert_ failed\n"); exit(2); } }
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); } else printf("ok:   "); printf(__VA_ARGS__); printf("\n"); } while (0)
static void set_now(uint64_t us) { stub_now_us = us; stub_timer_hw.timelr = (uint32_t)us; stub_timer_hw.timehr = (uint32_t)(us >> 32); }
static void feed(const char *s) { stub_rx = s; while (*stub_rx) GPStimeUartRxIsr(); }
static void pps(void) { GPStimePPScallback(2, GPIO_IRQ_EDGE_RISE); }
static void rearm(void) { if (stub_alarm_pending) { stub_alarm_pending = 0; stub_alarm_cb(0, NULL); } }
static const char *rmc(char *buf, const char *hhmmss, const char *ddmmyy, const char *lon, char ew, int corrupt) {
    char body[128]; snprintf(body, sizeof body, "GPRMC,%s,A,4041.000,N,%s,%c,0.0,0.0,%s,,,A", hhmmss, lon, ew, ddmmyy);
    unsigned c = 0; for (char *p = body; *p; p++) c ^= (unsigned char)*p;
    sprintf(buf, "$%s*%02X\r\n", body, corrupt ? (c ^ 0x11) : c); return buf; }
int main(void) {
    char b[160]; setenv("TZ", "UTC", 1);
    GPStimeInit(0, 9600, 2);
    /* PPS at t=10.000 s, RMC for 12:00:00 arrives 0.4 s later */
    set_now(10000000); pps(); CHECK(ppsTriggered && gu32_pps_count == 1, "first PPS accepted");
    CHECK(!stub_irq_enabled, "PPS IRQ disarmed after an edge");
    set_now(10400000); feed(rmc(b, "120000.00", "161126", "07400.000", 'W', 0));
    CHECK(gTimeContext._time_data._u32_utime_nmea_last == 1794830400u, "RMC time parsed: %lu (expect 1794830400 = 2026-11-16 12:00:00Z)", (unsigned long)gTimeContext._time_data._u32_utime_nmea_last);
    CHECK(gTimeContext._time_data.lon < -73.99 && gTimeContext._time_data.lon > -74.01, "West longitude is negative: %.4f", gTimeContext._time_data.lon);
    CHECK(gTimeContext._time_data._u64_pps_of_nmea_last == 10000000, "RMC anchored to its PPS edge");
    /* corrupted sentence rejected */
    uint32_t before = gTimeContext._time_data._u32_utime_nmea_last;
    set_now(10600000); feed(rmc(b, "235959.00", "010199", "07400.000", 'W', 1));
    CHECK(gTimeContext._time_data._u32_utime_nmea_last == before && gTimeContext._time_data._u32_nmea_bad_count == 1, "bad checksum rejected, time unchanged");
    /* RF glitch on PPS line 0.2 s after a real edge: ignored even if IRQ fired */
    set_now(10200000); ppsTriggered = false; pps();
    CHECK(!ppsTriggered && gu32_pps_rejected == 1, "spurious PPS edge 0.2 s after a real one is rejected");
    /* re-arm after 0.9 s, next real PPS at 11.000 */
    set_now(10900000); rearm(); CHECK(stub_irq_enabled, "PPS IRQ re-armed by the 900 ms alarm");
    set_now(11000000); pps(); CHECK(ppsTriggered && gu32_pps_count == 2, "next real PPS accepted");
    uint32_t t;
    CHECK(GPStimeGetTimeAtUs(11000000, &t) == 0 && t == 1794830401u, "time at PPS+1 = 12:00:01 (%lu)", (unsigned long)t);
    /* PPS and NMEA stop (receiver desensed). Holdover ticks at 12 s ... 130 s */
    CHECK(GPStimeGetTimeAtUs(12000000, &t) == 0 && t == 1794830402u, "holdover tick +1 s -> 12:00:02");
    CHECK(GPStimeGetTimeAtUs(130000000, &t) == 0 && t == 1794830520u, "holdover 120 s later -> 12:02:00 (%lu)", (unsigned long)t);
    /* tick jitter of +-0.3 s still rounds to the right second */
    CHECK(GPStimeGetTimeAtUs(130300000, &t) == 0 && t == 1794830520u && GPStimeGetTimeAtUs(129700000, &t) == 0 && t == 1794830520u, "+-0.3 s jitter rounds to the same second");
    /* stale sentence bytes: a long sentence then a short one must not re-parse old data */
    set_now(131000000); feed("$GPGSV,3,1,12,01,02,003,04,05,06,007,08,09,10,011,12,13,14,015,16*70\r\n$GPVTG*00\r\n");
    CHECK(gTimeContext._time_data._u32_utime_nmea_last == before, "non-RMC sentences leave time alone");
    /* N2GF GPSCAL: real PPS ISR -> PPSCal. Pico crystal 15 ppm slow (the case that made the
       original code publish 5.76e17), with RF glitches between pulses. */
    GPStimeInit(0, 9600, 2);
    { int32_t ppb = 0; uint64_t t0 = 200000000ULL;
      for (int n = 0; n < 40; n++) {
          uint64_t tn = t0 + (uint64_t)llround(n * 1e6 * (1 - 15e-6));
          set_now(tn); pps();
          set_now(tn + 300000); pps();              /* glitch: rejected by the edge filter */
          set_now(tn + 900000); rearm();
          if (n == 31) CHECK(PPSCalGet(&gPPSCal, tn, &ppb) == PPSCAL_NONE, "no correction before 32 full intervals");
      }
      int st = PPSCalGet(&gPPSCal, stub_now_us, &ppb);
      CHECK(st == PPSCAL_LOCKED && ppb > -15100 && ppb < -14900, "slow crystal measured as %ld ppb (expect -15000), not 5.76e17", (long)ppb);
      CHECK(gTimeContext._time_data._i32_freq_shift_ppb == ppb, "status field mirrors the safe value");
      CHECK(PPSCalGet(&gPPSCal, stub_now_us + 600000000ULL, &ppb) == PPSCAL_HELD && ppb > -15100 && ppb < -14900, "PPS lost 10 min: last good value held");
    }
    printf("\nRESULT: %s\n", fails ? "FAIL" : "PASS"); return fails != 0;
}
