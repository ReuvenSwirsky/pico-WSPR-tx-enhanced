# Bring-up / test notes — Pico 2 (RP2350) at 300 MHz, 10 m, GPS

These notes cover the first power-up of the `claude-fable` build on a Pico 2 and
how to tell, step by step, whether each new piece actually works. Work through
them in order; each stage only depends on the ones before it.

What changed in this build (see README items 19–21 for the long version):

- RP2350 runs at 300 MHz (was 150). Flash divider set to 3, core 1.20 V, clk_peri 48 MHz.
- GPS PPS frequency correction is live again (it was compiled out before).
- Boot prints the system clock, the oscillator ceiling and the Tx frequency.

Nothing about pins changed. Same board, same wiring.

## 0. What you need

- Pico 2 (plain, not W) on your existing board.
- GPS module with PPS, wired as before: PPS → GPIO 2 (pin 4), GPS TX → GPIO 1 (pin 2, UART0 RX).
  Note: the README has said GPIO 4 for a long time but the code uses GPIO 1 — check which one your board actually uses.
- Start button on GPIO 21 (pin 27) to 3V3 — needed for the GPS-off tests.
- A receiver that covers 28 MHz (SDR, HF rig) with a waterfall, or at minimum a frequency counter on the RF pin.
- A dummy load or a short wire on GPIO 6 — **no antenna** until the signal has been checked on the waterfall. The output is a square wave with harmonics at 56, 84, 112… MHz.
- Serial terminal (115200 8N1) on the USB CDC port.

## 1. Build and flash

```
cmake -S . -B build-pico2 -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=pico2
ninja -C build-pico2
```

Hold BOOTSEL, plug in, copy `build-pico2/pico-wspr-tx-enhanced.uf2` to the RPI-RP2 drive.

If you also want a Pico 1 reference build for A/B comparison, use `-DPICO_BOARD=pico` into a separate directory. The RP2040 build is unchanged apart from the GPS correction being re-enabled.

## 2. Stage A — does it boot at 300 MHz? (no GPS, no RF checks yet)

This is the one thing that can fail silently: if the chip or flash is unhappy at 300 MHz, you get nothing on USB at all.

1. Hold the button while plugging in USB (forces the settings menu).
2. Open the serial port. You should see the settings prompt within a couple of seconds.
   - **No serial port appears at all** → the clock change failed (silicon or flash). See §7 "Fallbacks".
   - Port appears but garbage/no prompt → same thing, lower the clock.
3. Type `EXIT` then `REBOOT`, or power cycle without the button. The first lines should be:

   ```
   Sys clock 300 MHz, DCO max 37500000 Hz, Tx 28126xxx Hz
   ```

   (The Tx value depends on BAND/OFFSET/CALPPM. If BAND is not 10 yet, that's fine — the number is what matters.)

   - If it says `Sys clock 150 MHz` the RP2350 branch of `defines.h` didn't take — you built for the wrong board.
   - If a `WARNING: … above the … limit` line appears, the band is beyond sysclock/8. On a Pico 2 at 300 MHz that cannot happen for any listed band; on a Pico 1 it will for 12 m and 10 m.

4. Let it sit for a minute. The LED should do the very-slow flash (waiting for button/GPS). A reset loop here (LED pattern restarting, serial port dropping) means the watchdog is firing — note it, it matters in Stage D.

Pass criteria: stable serial, correct boot line, no resets for 1 minute.

## 3. Stage B — settings for the test

In the settings menu (button held at power-up):

```
CALLSIGN <yours>
LOCATOR <4 or 6 char>
MODE WSPR
BAND 10
GPS ON
GPSLOCATION OFF        (keep the locator fixed for now — one variable at a time)
FREQHOP OFF            (fixed frequency makes it easy to find on the waterfall)
OFFSET 0               (transmit in the middle of the 200 Hz WSPR window)
CALPPM 0               (start at zero so the GPS correction is measured, not masked)
SLOTSKIP 0             (transmit every slot while testing)
POWER 10               (or whatever you actually run; it's only the reported value)
EXIT
```

The dial frequency for 10 m is 28.126000 MHz; WSPR tones sit 1400–1600 Hz above it, so with OFFSET 0 expect the carrier at about **28.127500 MHz** (±crystal error, see Stage C).

## 4. Stage C — RF sanity with GPS OFF (button start)

Purpose: prove the 300 MHz oscillator produces a clean, correct 28 MHz signal before GPS is in the picture. This isolates "clock/PIO problem" from "GPS problem".

1. `GPS OFF` in settings, exit, reboot.
2. Receiver on 28.126 MHz USB, waterfall running, ~3 kHz span.
3. Press the button. LED goes to fast flash, serial prints `WSPR> Start TX.`
4. Look at the waterfall for the ~111 s transmission:
   - **Expected**: a single narrow 4-FSK trace about 6 Hz wide, roughly 1500 Hz above the dial, drifting very little. On a Pico 2 crystal (±20–30 ppm) the absolute error can be up to ±800 Hz at 28 MHz, so it may land outside the 200 Hz WSPR window — that is fine at this stage, we are checking shape, not placement.
   - Spurs ("picket fence" of weaker lines spaced a few kHz either side) are normal for this synthesis method, see the README. They should be well below the main carrier.
   - **Bad**: wide/smeared trace, multiple equal-strength lines, signal jumping around by hundreds of Hz, or no signal → core 1 is not keeping the PIO fed. That is the failure mode the 300 MHz clock is supposed to remove. Jump to §6.
5. Note the measured carrier frequency. `(measured − expected) / 28.1275 MHz × 1e6` is your crystal error in ppm. Write it down — Stage D should discover the same number.
6. Serial should end with `WSPR> End Tx. @ 112 secs` or thereabouts. If the time is way off (e.g. 110 or 115) the symbol timer is off; unlikely but note it.
7. Try to decode: feed receiver audio to WSJT-X in WSPR mode. If the carrier was outside the window, temporarily set `CALPPM` to the negative of the measured ppm (e.g. measured +18 ppm → `CALPPM -18`) and repeat; it should now decode.

Pass criteria: clean trace, decodes with CALPPM compensation, no resets.

## 5. Stage D — GPS ON (the actual test)

Purpose: the "GPS + >20 m" problem. Everything from Stage C must already pass.

1. Set `CALPPM 0` back (we want to see the GPS do the work). `GPS ON`. Exit, reboot. GPS outside or on a window sill; cold start can take minutes.
2. Serial at boot: `Init GPS`, then `Wait for GPS PPS pulses ....` with dots until PPS arrives, then `GPS PPS received`. If you never get past the dots, PPS is not reaching GPIO 2 (wiring, module not locked, or wrong pin). If `Init GPS` is followed straight by waiting for the button, no NMEA was seen on GPIO 1 within 2 s — check the GPS TX wire and that the module is 9600 baud.
3. Once locked, the `Slot n:ss Rx/Tx` line prints every second. Let it run **at least 40 s before the first Tx** — the PPS filter needs a full 32 s window before it produces a correction. It will transmit at the next even minute, so if the first slot comes too early just treat it as warm-up.
4. On the second and later transmissions, measure the carrier again:
   - **Expected**: the carrier is now within a few Hz of 28.127500 (plus OFFSET), i.e. the crystal error measured in Stage C has been removed. During the 111 s it should not drift more than ~1 Hz.
   - If it is still off by the Stage C ppm → the correction is not being applied. Check `GPS_PPS_FREQ_CORRECTION` is 1 in `defines.h` and that the build is from the right branch.
   - If it is off by a **large, different** amount, or swings around → correction is producing garbage. That is the class of bug that was fixed; report the measured frequencies and ppm.
5. Leave it running for 30+ minutes / 15 cycles. Watch for:
   - any reset (boot banner reappearing, serial port dropping),
   - `Slot` lines stopping while the LED keeps flashing (core 0 stuck),
   - transmissions that start but never print `End Tx`,
   - a transmission whose trace is clean for a while and then smears.
6. Decode a few with WSJT-X, check reported dT is small (|dT| < 1 s) — that validates the PPS-based start timing.
7. Now turn on `GPSLOCATION ON` and `FREQHOP ON` and repeat a few cycles. These add the remaining code paths (maidenhead calc each loop, random offset).

Pass criteria: ≥15 consecutive clean, decodable cycles with GPS on, carrier within a few Hz of nominal, no resets.

## 6. If GPS ON fails but GPS OFF passes

This is the interesting case, because it says the problem is not the oscillator ceiling. Separate the two remaining theories:

**Core 1 starvation (bus load from core 0).** Symptom: trace smears/jumps during Tx, no reset. Test: turn off the per-second debug print (`#define DEBUG_PRINT` in `main.c`) and rebuild. If that noticeably helps, it's load. A definitive check is a PIO TX-FIFO stall counter (`pio0->fdebug` TXSTALL bit for the DCO state machine) printed once a second; ask for it to be added.

**RF getting into the GPS wiring.** Symptom: PPS or NMEA goes bad only while transmitting (e.g. `Slot` seconds jump, `GPS PPS` lost, locator changes, time goes wrong), possibly the watchdog resets. Test: move the GPS and its wires away from the RF pin / dummy load, or temporarily drop the RF pin drive strength; put a 1 k series resistor in the PPS line. If it goes away, it's coupling, and the fix is layout/filtering, not firmware.

**Watchdog reset.** If you see resets, read `watchdog_caused_reboot()` at boot (not currently printed; trivial to add) to distinguish a crash from a power glitch.

For either theory, the single most useful thing to record is: the band, GPS on/off, whether the trace was clean, and the serial log around the failure.

## 7. Fallbacks if Stage A fails (won't boot at 300 MHz)

In `defines.h`, change the RP2350 line:

```
#define PLL_SYS_MHZ PLL_SYS_MHZ_OVERCLOCK_300MHZ
```

to 250 MHz (add `#define PLL_SYS_MHZ_OVERCLOCK_250MHZ 250UL`). 10 m still fits (ceiling 31.25 MHz) but with less core-1 margin. Alternatively in `init.c` raise `VREG_VOLTAGE_1_20` to `VREG_VOLTAGE_1_25`. Do one at a time so you learn which one mattered.

If the board boots at 150 MHz (comment out the RP2350 branch) but not at 250/300, the flash is the usual suspect: try `SetFlashClkDiv(4)` in `init.c`.

## 8. Pico 1 regression check (optional but recommended)

Flash the `-DPICO_BOARD=pico` build onto your old Pico 1 on the same board and run Stage C and D on **20 m** (`BAND 20`). This is the configuration Roger had working, and it now has the GPS correction enabled. If 20 m with GPS still works and the carrier is now GPS-corrected, the correction fix is good independently of the Pico 2 work. Then try 17 m with GPS: expect it to be marginal (the README explains why); that is known and not a regression.

## 9. What to report back

- Boot line (`Sys clock … Tx …`).
- Stage C measured frequency and derived ppm.
- Stage D measured frequency on the 2nd+ transmission, and drift over a transmission.
- Number of clean cycles, number of failures, what each failure looked like (serial + waterfall).
- Whether resets occurred.

With those numbers the remaining uncertainty (see the "how sure are you" discussion — starvation vs RF pickup vs something else) can be closed out quickly.
