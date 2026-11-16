# pico-WSPR-tx-enhanced on a Raspberry Pi Pico 2, 10 m: N2GF build

Based on rogerclarkmelbourne/pico-WSPR-tx-enhanced, commit 57d0aa0, with the RPiks pico-hf-oscillator code that is vendored in that repo.
Your board's pinout is unchanged: RF on GP6, GPS on UART0 (RX GP1, TX GP0) at 9600 baud, PPS on GP2, button from GP21 to 3.3 V, LED on GP25.

**Status:** this compiles, and the math, the GPS/time code and the new GPS frequency calibration pass tests on a PC. **None of it has run on a real Pico 2 yet.**
The steps in section 5 are how you prove it.

## 1. Files

| File | What it is |
|---|---|
| `pico2_10m_288MHz_fine_MAIN.uf2` | **Use this one.** Pico 2 (RP2350, not the W), 288 MHz, new "fine" RF engine |
| `pico2_10m_288MHz_classic_FALLBACK.uf2` | Same, but with the original RF engine. Use it only if MAIN gives no clean carrier. It has much stronger spurs. |
| `pico1_200MHz_classic_regression.uf2` | Original Pico (RP2040) at 200 MHz with the same GPS fixes. **It cannot do 10 m.** |
| `n2gf-10m-pico2.diff`, `0001-*.patch`, `0002-*.patch` | All source changes against 57d0aa0 (one combined diff; two `git am` patches: the 10 m port, then the GPS calibration) |
| `tests/` | Results from the PC-side tests and the spur model. `hosttest/RUN_TESTS.sh` reruns them all |
| `hosttest/` | Test sources: `dco_math_test.c`, `fine_engine_test.c` (now also checks GPS/CALPPM correction), `gps_test.c` (runs the real GPStime.c), `ppscal_test.c` (new GPS calibration), `upstream_pps_test.c` (reproduces the original GPS-correction bug), plus the spur models |
| `build.sh` | Rebuild script: `build.sh <name> pico2 288 1` |

## 2. What changed

1. **Clock is a build option.** `-DWSPR_SYSCLK_MHZ=288`. If you leave it unset, you get the original 200 MHz.
   - On the RP2350 it does the following, in this order:
     - Core voltage to 1.30 V (the most allowed without unlocking the limit).
     - QSPI flash slowed to clk_sys/4 = 72 MHz, with RX sample delay = 4 half-cycles.
     - Then clk_sys goes to 288 MHz (PLL VCO 1440 MHz, /5/1).
   - clk_peri stays at 48 MHz, because the RP2350 spec limits it to 150 MHz.
   - `PICO_FLASH_SPI_CLKDIV` does nothing on RP2350, which uses no boot2. That's why the QMI timing is set directly in `init.c`.
2. **Saving settings is overclock-safe.** After a flash erase or program, the bootrom puts the flash clock back to clk_sys/3, which would be 96 MHz. `settingsWriteToFlash()` now runs from RAM and re-applies the safe timing before any code runs from flash again.
3. **New "fine" RF engine** (`dco3.pio`, `PioDCOWorker3`, built with `-DWSPR_DCO_FINE=1`):
   - The original PIO program reused one delay value for 4 half-cycles, so the phase moved in 4-clock steps. The new one gives every half-cycle its own count, so the phase moves in 1-clock steps.
   - The frequency math is scaled per band (2^22 to 2^27). Resolution on 10 m is about 0.01 Hz.
4. **Out-of-range bands are refused.** If the band is above what the build can generate (clk_sys/8: 25 MHz at 200 MHz, 36 MHz at 288 MHz), the firmware prints an error and won't transmit. Before, it put out garbage.
5. **GPS fixes:**
   - **PPS holdover:** a missing PPS no longer stops the scheduler or starves the watchdog. See section 3.
   - **PPS edge filter:** after a PPS edge, the interrupt is disabled for 900 ms.
   - **NMEA checksum is now checked.** It was a FIXME.
   - **Field offsets fixed:** RMC field offsets are now taken from the start of the `$GPRMC` sentence, and the sentence buffer is NUL-terminated.
   - **West longitude fixed:** the code read the N/S field to decide E/W, so West longitudes came out East and GPSLOCATION gave the wrong grid in the Americas.
   - **Race fixed:** the GPS context pointer could be wiped by core1 at startup.
6. **GPS frequency calibration is back, rewritten safely** (`pico-hf-oscillator/gpstime/ppscal.c`). Section 3b explains why the original was switched off.
   - It measures the Pico crystal against GPS PPS over 32-second windows, in signed 64-bit math.
   - A window counts only after 33 unbroken PPS pulses, each 1 s ±150 µs. A missing or extra pulse starts a new run; it is never compared against empty slots.
   - Windows outside ±100 ppm are rejected. The result is smoothed (IIR, 1/16), and jumps of more than 2 ppm are ignored unless they repeat 10 times. The applied value is clamped to ±100 ppm.
   - If PPS is lost (for example, your own signal desensing the GPS), the last good value is kept.
   - The correction is now applied to every frequency the transmitter sets (WSPR tones, frequency hop, carrier set-up and CW), using f·e/(1+e) with the units checked.
   - New setting **`GPSCAL ON|OFF`**, default ON. It is only used when `GPS ON`.
   - **CALPPM is the fallback:** it is used until PPS has locked (about 33 s after the first PPS), always when GPSCAL or GPS is OFF, and if the GPS never gives a sane measurement. With a GPS lock, CALPPM is ignored, so it doesn't double-correct.
   - Your existing settings are kept, because the new field uses a magic value and no version bump. Old flash reads as GPSCAL ON.
   - The PPS interrupt now has higher priority than the UART and timer interrupts, so the timestamps jitter less.
7. **Tx diagnostics.** At the end of every transmission the serial log prints:
   - `DCO FIFO underrun during Tx: no/YES`
   - PPS counts (accepted and filtered)
   - Bad-NMEA count
   - `GPS PPS missing - holdover tick N`, if PPS dropped during Tx
   - `Freq corr: -15000 ppb (GPS locked | GPS held, PPS lost | CALPPM) windows:… ivl-rej:… win-rej:… outliers:…`

## 3. The "hangs above 20 MHz with GPS" bug

**No integer overflow found.** I checked every frequency-dependent multiply:
- TxChannel.c:78 uses `1000LL`.
- piodco.c:123-125 stays in int64.
- The ppb correction at piodco.c:158-159 only truncates for |ppb| above about 1e11.

Also, in Roger's fork the GPS ppb correction is compiled out (`#ifdef FIX_BUGS_IN_THIS`, GPStime.c:116), so its result is always 0. Section 3b covers what was wrong with it.

**Most likely root cause** (code-level evidence; not reproduced on hardware):
- In GPS mode, the main loop's 1-second tick comes only from the PPS interrupt (`while(!ppsTriggered)`, main.c:118).
- The watchdog is fed only after that tick (main.c:123), and it is a 10 s watchdog (main.c:273).
- If PPS stops while transmitting, the loop blocks, and the watchdog resets the Pico in the middle of the transmission. After the reboot, main() waits for PPS again (main.c:221). That looks exactly like a hang or crash.
- Without GPS, a crystal timer makes the tick, so this can never happen. That's why it's GPS-only.

**Why PPS stops on the higher bands:**
- The beacon is an unfiltered 3.3 V square wave with heavy edge jitter.
- On 10 m, its 56th harmonic is 56 × 28.126 = 1575.06 MHz, only 0.36 MHz from GPS L1 (1575.42 MHz) and inside the C/A main lobe. Higher fundamentals also put more energy near L1.
- Pickup on the PPS wire (GP2) can also fire false edges.
- I can't prove this without hardware. The holdover and edge filter fix the firmware side either way.

**Separate bug, not GPS-related: the frequency ceiling** (piodco.c:127 and :199-202).
- Above clk_sys/8, `si32precise_cycles` goes negative and the worker emits bursts of y = 255. The output collapses to under 1 MHz of garbage.
- At 200 MHz the ceiling is 25 MHz, so 10 m was impossible. 12 m sits right at the edge: the model puts a −0.2 dBc spur at 25.07 MHz.

## 3b. Was the GPS frequency correction the cause? No. It was broken, though, and it's now fixed.

**Verdict:**
- The original GPS PPS correction has a real, **confirmed bug**: whenever the Pico crystal runs slow, it puts the transmitter **0.2-1 MHz off frequency**.
- It **cannot hang or crash** the CPU, and it is **not** the cause of Roger's "hangs above 20 m with GPS". The evidence is below.

**The bug.** It is in `pico-hf-oscillator/gpstime/GPStime.c:137` in Roger's fork (line 139 in RPiks/pico-WSPR-tx and line 141 in RPiks/pico-hf-oscillator):
```c
_i32_freq_shift_ppb = (_u64_pps_period_1M - (int64_t)1e6*1e6*32 + 16) / 32;
```
- `_u64_pps_period_1M` is `uint64_t` (GPStime.h:97), so the whole expression is evaluated **unsigned**.
- If the measured period is even 1 µs shorter than nominal (the crystal is slow), it wraps to about 2^64 and is divided unsigned by 32. The result is **576460752303423488**. That is VK3KYY's "returns huge positive numbers" (commit bc0b125).
- `PioDCOGetFreqShiftMilliHertz()` (piodco.c:159) then multiplies that by f in kHz. This overflows int64, gets truncated to int32, and is fed in as `-2*comp` mHz (TxChannel.c).
- About half of all crystals are slow. With PPS jitter, even a perfect crystal flips sign about half the time.
- Fast crystals were corrected correctly. The odd "ppb" units (really 1e-12) cancel against the kHz scaling.

**Test evidence.** `hosttest/upstream_pps_test.c` runs the original code copied verbatim, with realistic PPS sequences: startup, normal, missing pulse, spurious pulse and ±3 µs jitter. Results are in `tests/upstream_pps_test.txt`.
- Crystal −0.5, −5 or −20 ppm: the field reads 5.7646e17 after 200 s in every sequence. 24 of 27 slow-crystal/band cases transmit more than 1 kHz off. Examples: 10 m at −277 kHz, 20 m at −1.005 MHz, 30 m at −554 kHz. 40 m happens to land correctly, by overflow coincidence.
- Crystal 0 ppm with jitter: garbage again (83 of 200 updates).
- Startup: the first 32 pulses are compared against zeroed slots. If the first PPS arrives at uptime 32 s ±8 ms (about 1.6 % of boots), a bogus seed is accepted. That error is bounded and decays.
- Missing or spurious pulses shift the window to 33 s or 31 s. Those windows are correctly rejected, so no garbage comes from them.

**Why it isn't the hang:**
- **Timeline:** the correction was compiled out on 2025-12-25 (bc0b125). Roger recorded "bands above 20m don't seem to work with GPS enabled… firmware hang" on 2025-12-28 (85146c8), and the code was already disabled by then.
- **No trap path:** every operation is integer math with constant divisors. The SetFreq denominator, 2000·f + m, stays above 0 for any int32 m when f > 1.07 MHz, so it can't divide by zero.
- **No crossing into the garbage region:** the bogus shift is at most about ±1 MHz on 20 m and about −277 kHz on 10 m. With the original code at 270 MHz it never pushes a band over the clk_sys/8 ceiling, so it gives a wrong frequency, not a stall.

**Most likely cause, unchanged:** PPS loss while transmitting stalls the PPS-driven main loop until the 10 s watchdog fires (section 3).
- A second mechanism with the same symptom: RF picked up on the PPS wire fires the unfiltered PPS interrupt many times. The extra `ppsTriggered` ticks scramble the slot timing, and an interrupt flood can starve core0.
- The holdover tick and the 900 ms PPS edge filter in this build cover both.
- None of this is proven on hardware yet. Test step 5.5 is how you'd see it.

## 4. Flashing and settings

1. Hold **BOOTSEL** on the Pico 2, plug in USB, then let go. A drive called `RP2350` appears.
2. Drag `pico2_10m_288MHz_fine_MAIN.uf2` onto it. The Pico reboots on its own.
3. Settings live in the Pico's own flash, so a new Pico 2 starts with defaults.
   - Open the Pico's USB serial port in a terminal; any baud works.
   - To force the settings menu, hold the GP21 button while plugging in.
   - Enter the settings below one per line, then type `EXIT` (or `REBOOT`).
   ```
   CALLSIGN N2GF
   LOCATOR FN20xx        (your grid; or GPSLOCATION ON to take it from GPS)
   MODE WSPR
   BAND 10               (selects 28.126000 MHz + 100 Hz; dial 28.1246 MHz)
   GPS ON
   RFPIN 6
   POWER 10              (the dBm you report, use your real output)
   SLOTSKIP 4            (transmit every 5th slot, or as you like)
   CALPPM 0              (fallback before GPS lock / without GPS, see 5.2)
   GPSCAL ON             (default; OFF = use CALPPM only)
   EXIT
   ```
4. At boot, the serial log should show `N2GF build: sysclk 288 MHz, DCO fine (dco3) engine, max RF 36000000 Hz`.

**Low-pass filter (required).** GP6 puts out a 3.3 V square wave, so the 3rd harmonic is at 84.4 MHz and only about 10 dB down.
- Use at least a 5- to 7-pole 10 m low-pass filter (cutoff around 30-32 MHz), such as the QRP Labs 10 m LPF kit. Put a series resistor or matching on the GPIO, or follow it with a buffer/PA.
- The filter will **not** remove the DCO's own spurs below and near the carrier. See section 6.

## 5. Test steps

Do these into a dummy load first. Put **30-40 dB of attenuation** in front of the SDR.

1. **It runs:** watch the serial log for the banner, `Slot n:s` lines every second, and `WSPR> Start TX.`. The LED blinks fast while transmitting.
2. **Frequency:** tune the SDR to 28.1261 MHz with a narrow span (zoom FFT, under 1 Hz per bin). You should see 4 tones about 1.46 Hz apart, roughly 6 Hz wide in total.
   - Measure the offset from 28,126,100 Hz plus or minus the symbol offset.
   - The Pico's crystal is typically off by 10-30 ppm, which at 10 m is **280-840 Hz**. That's outside the 200 Hz WSPR window.
   - **With GPS ON and GPSCAL ON**, wait for `Freq corr: … (GPS locked)` in the log (from about 33 s after PPS starts). The carrier should then be within about ±1 Hz of 28,126,100 Hz plus the symbol offset, with no CALPPM. The log's ppb number is your crystal error: −15000 means 15 ppm slow.
   - **Also set CALPPM** to cover the first ~33 s, GPS OFF, or a GPS that never locks: measure with `GPSCAL OFF` and `CALPPM 0`, then compute it as below. Or take it from the log: CALPPM ≈ −ppb/1000, so −15000 ppb gives CALPPM 15.
   - `CALPPM` = −(measured − 28126100) / 28.1261, rounded to a whole ppm. Example: 300 Hz high gives CALPPM −11. One step is about 28 Hz.
   - Let it warm up for about 10 minutes before measuring. At 1.30 V the chip runs warmer, and 1 ppm of drift is 28 Hz on 10 m.
3. **Harmonics and spurs:**
   - Check 84.4 MHz (3rd) and 140.6 MHz (5th) after the filter.
   - Sweep 1-30 MHz, looking especially around 20-23 MHz and 27.5 MHz, and note any spur within 1 MHz of 28.126 MHz. Compare against the model in `tests/spur_model.txt`.
   - Section 6 covers what passes.
4. **CPU keeps up:** after each Tx, the log should say `DCO FIFO underrun during Tx: no`. `YES` means core1 couldn't keep the PIO fed, and the signal is disturbed. Tell me if you see it.
5. **GPS while transmitting:** run with GPS ON and watch for `GPS PPS missing - holdover tick` during Tx.
   - If it appears, your own RF is desensing the GPS. That confirms the theory, and the beacon should keep running.
   - Fixes: keep the GPS antenna well away from the RF output and its antenna, use the LPF, and use short or ferrite-choked GPS leads.
   - `filtered:` counting up means false PPS edges are being rejected.
   - `Freq corr: … (GPS held, PPS lost)` during or after Tx is expected if your own RF knocks the GPS out. The last good correction stays applied.
   - A slowly changing ppb (tens of ppb per minute after power-up) is normal crystal warm-up. `outliers:` climbing steadily means PPS timing is noisy, so check the PPS wiring.
6. **On the air:** connect the antenna through the LPF. After a few slots, search wsprnet.org (Database → Spots, call N2GF, band 10 m) or wspr.rocks.
   - Check that reported frequencies fall between 28.126000 and 28.126200 MHz and that drift is 0 or ±1.
   - 10 m openings come and go, so give it a day or two before deciding it doesn't work.

## 6. Risks and limits (please read)

- **Overclock stability:**
  - 288 MHz at 1.30 V is a common RP2350 overclock, but it's beyond the 150 MHz spec, and individual chips vary.
  - If it freezes or reboots, try the FALLBACK file. If both fail, the chip may not handle 288 MHz; ask me for a 276 MHz build, which still gives a 34.5 MHz ceiling.
- **Flash timing:**
  - 72 MHz SCK with RXDELAY = 4 matches what MicroPython uses on RP2350, but it hasn't been tested on your Pico 2.
  - Symptoms would be a crash right at boot or right after saving settings.
- **CPU margin:**
  - On 10 m at 288 MHz the worker has about 20.5 clock cycles per FIFO word. I estimate it needs 15-18 from the disassembly, but that's not measured.
  - The underrun line in the log is how you'd know.
- **Spurs:** this is a purely digital synthesizer. The model in `tests/spur_model.txt` assumes ideal edges:
  - **Fine engine on 10 m:** worst spur about −25 dBc between 1 MHz and 0.9f (at 20.8 MHz), and about −26 dBc at 27.54 MHz, 585 kHz below the carrier.
  - **Classic engine:** about −15 dBc. The original firmware on 20 m models at about −9 dBc, 0.66 MHz away.
  - **FCC rule:** §97.307 asks for spurious emissions at least 43 dB down. A 10 m LPF won't reach that for spurs below the carrier, and only a very narrow (crystal-type) filter would for spurs near it.
  - **Bottom line:** measure before putting it on an antenna. For a clean 10 m beacon, a Pico driving an Si5351 with a TCXO is still the better path.
- **Pico 2 erratum RP2350-E9** (A2 chips): an input held only by the internal pull-down can float at about 2.2 V after being driven high. Your button pulls GP21 to 3.3 V with an internal pull-down, so add a 4.7-8.2 kΩ resistor from GP21 to GND. Newer A4-stepping Pico 2 boards fixed this.
- **Existing quirk, not changed:** UART stdio is enabled on UART0 (GP0/GP1), so debug printf text also goes out GP0 to the GPS module's RX at 9600 baud. Most NMEA receivers ignore it.
- **Board type:** built for the standard Pico 2 board (LED on GP25, 4 MB flash). A "Pico 2 Zero"-style board (RP2350-Zero) has a different LED and pin layout and would need its own build.
