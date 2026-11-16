# Bring-up: Waveshare RP2350-Zero as a drop-in for the RP2040-Zero (N2GF 10 m WSPR)

Firmware: branch `grokbot` (Pico 2 / RP2350 port, 288 MHz, fine DCO engine, GPSCAL).
Sources checked (Nov 2026):
- Waveshare wiki pinout and dimension drawings for both RP2350-Zero and RP2040-Zero
- Waveshare schematics `RP2350_Zero.pdf` and `RP2040_Zero.pdf`
- Pico SDK 2.2.0 board headers `waveshare_rp2350_zero.h` and `waveshare_rp2040_zero.h`

**Nothing here has been tried on a real RP2350-Zero yet.**

## 1. Is the RP2350-Zero a drop-in for the RP2040-Zero?

**Castellated edge (all 23 pads): yes, identical.**
- Same 18.00 × 23.50 mm outline and 2.54 mm pitch.
- Same pin order and the same GPIO on every edge pad:

| Edge (USB at top) | Pads, in order | Same on both? |
|---|---|---|
| Right, top → bottom | GP0 GP1 GP2 GP3 GP4 GP5 GP6 GP7 GP8 | yes |
| Bottom, right → left | GP9 GP10 GP11 GP12 GP13 | yes |
| Left, top → bottom | 5V GND 3V3 GP29 GP28 GP27 GP26 GP15 GP14 | yes |

**Bottom solder pads (10 pads, not castellated): NOT the same. They are shifted by one pad.**
Both Waveshare's drawings and the schematics show this. On the schematics, RP2040-Zero has P2 = GP17-GP21 and P3 = GP22-GP25 + GND, while RP2350-Zero has P2 = GND + GP17-GP20 and P3 = GP21-GP25.

| Pad position (from the USB end) | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|
| RP2040-Zero | **GND** | GP25 | GP24 | GP23 | GP22 | **GP21** | GP20 | GP19 | GP18 | GP17 |
| RP2350-Zero | **GP25** | GP24 | GP23 | GP22 | **GP21** | GP20 | GP19 | GP18 | GP17 | **GND** |

**Other differences:**

| | RP2040-Zero | RP2350-Zero | Impact |
|---|---|---|---|
| MCU | RP2040 | RP2350A (QFN-60, GP0-GP29, same as RP2040) | Needs the RP2350 firmware (this branch); RP2040 UF2s won't run |
| RGB LED | WS2812B on GP16 | WS2812B on GP16 | Same. No plain LED on either board, and GP16 is not on any pad |
| 3V3 regulator | RT9013-33 LDO | ME6217C33 LDO, 800 mA | Fine |
| Core regulator | RP2040 internal LDO | RP2350 internal switcher plus a 3.3 µH inductor on the board | Fine. The firmware raises the core voltage to 1.30 V |
| Crystal | 12 MHz | 12 MHz, 15 pF loads | Ordinary crystal, tens of ppm. GPSCAL or CALPPM corrects it |
| Flash | W25Q16JV, 2 MB | Schematic says **W25Q16JVUXIQ, 2 MB**; product page and SDK header say 4 MB | See §2. Both chips are fine at our 72 MHz flash clock (rated 133 MHz) |
| USB-C, 5V pad | 5V pad = USB VBUS | 5V pad = USB VBUS (no diode on the schematic) | Same. Don't feed 5 V into the pad while USB is plugged in unless your carrier has a diode |
| Buttons | BOOT, RESET | BOOT, RUN (same function) | Same |
| SWD | pads | SWCLK/GND/SWDIO pads at the USB end, on the back | n/a |

## 2. What the firmware uses, and what to check on the Zero

Firmware pins (`defines.h`, `main.c`): RF **GP6** (`RFPIN`, can be changed in settings), GPS UART0 **TX GP0 / RX GP1** at 9600, PPS **GP2**, button **GP21** to 3V3 with the internal pull-down, LED **GP25** (from `PICO_BOARD=pico2`).

| Signal | GPIO | Where it is on the Zero | Status on the RP2350-Zero |
|---|---|---|---|
| RF out | GP6 | right edge, pad 7 | **OK**, same pad |
| GPS TX→RX (Pico TX) | GP0 | right edge, pad 1 | **OK** |
| GPS RX←TX (Pico RX) | GP1 | right edge, pad 2 | **OK** |
| PPS | GP2 | right edge, pad 3 | **OK** |
| 3V3 / GND / 5V | — | left edge, pads 1-3 | **OK** |
| Button | GP21 | **bottom pad** | **Moved one position.** See below |
| "LED" | GP25 | bottom pad | **Lands on the RP2040-Zero's GND pad position.** See below. No visible LED either way |

**Button (GP21).**
- If the button is a wire to the module's bottom GP21 pad: solder it to the RP2350-Zero's GP21 pad, which is pad **5** from the USB end, not pad 6.
- If your carrier PCB has pads under the module that get soldered to it: the carrier's "GP21" pad (position 6) meets **GP20** on the RP2350-Zero, so the button won't work. Fixes:
  - (a) cut and jumper it to position 5, or
  - (b) TODO: a firmware option `BTN_PIN=20` (not done yet), or
  - (c) do without the button. It is only needed with GPS OFF, and to force the settings menu at boot. Without it, the menu appears only when the stored settings are invalid (first boot on a new board). To change settings later, erase flash (`flash_nuke.uf2`) and re-flash.
- Either way, add the 4.7-8.2 kΩ resistor from GP21 to GND. This is the RP2350-E9 pull-down erratum on A2 silicon; Waveshare's board photo shows an RP2350A0**A2** chip.

**GP25 / LED.**
- The pico2 build drives GP25 as an LED output that toggles.
- On the RP2350-Zero, GP25 is bottom pad 1, which is the pad where the RP2040-Zero has **GND**. If your carrier ties that position to ground, the firmware will drive a GPIO into a short.
- **Check with a meter before soldering the module down.** If it is grounded, leave that pad unsoldered or use the TODO LED-pin option.
- Bottom pad 10 is GND on the RP2350-Zero but GP17 on the RP2040-Zero. That's harmless unless your carrier uses GP17.
- The onboard WS2812 (GP16) stays dark with this firmware. Status comes from serial logs only.

**Flash size.** Settings live in the last 4 KB sector of flash.
- A pico2 or `waveshare_rp2350_zero` build assumes 4 MB, so settings go at 4 MB − 4 KB.
- If the chip really is the 2 MB W25Q16 shown on the schematic, that address doesn't exist, and saving settings may fail or wrap.
- **Use the 2 MB-safe build** (§3). It works on both 2 MB and 4 MB parts.
- To check: with the board in BOOT mode, `picotool info -a` should report the flash chip size.

**SDK board file.**
- `waveshare_rp2350_zero` exists in SDK 2.2.0, but it **does not define `PICO_DEFAULT_LED_PIN`**, so this firmware **fails to compile** with it. This was tested: errors in `init.c:109`, `TxChannel.c:179`, `WSPRbeacon.c:68` and `lib/assert.c:76`.
- It compiles with `CFLAGS=-DPICO_DEFAULT_LED_PIN=<pin>`. Its other settings make no difference to this firmware:
  - Boot2 is ignored on the RP2350.
  - Its `PICO_SMPS_MODE_PIN 23` is copied from the Pico 2 header. The Zero has no SMPS, GP23 is a free pad, and the firmware never touches it.

## 3. Recommended build (no code changes)

Keep `PICO_BOARD=pico2`, but force a 2 MB flash layout:
```
cmake -S . -B build-zero -G Ninja -DPICO_BOARD=pico2 -DWSPR_SYSCLK_MHZ=288 -DWSPR_DCO_FINE=1 \
      -DPICO_FLASH_SIZE_BYTES=2097152 -DCMAKE_BUILD_TYPE=Release && ninja -C build-zero
```
- This was built and checked: settings go to 0x1FF000.
- The prebuilt file is `pico2_10m_288MHz_fine_ZERO-2MB.uf2`, SHA-256 `b1880847…22b8f2`.
- The overclock and flash timing are the same as on the Pico 2: 288 MHz, 1.30 V, flash at 72 MHz with RXDELAY 4.

**TODO (code, not done):**
1. Add a `BOARD_ZERO` build option: LED pin unset or moved off GP25, and the WS2812 on GP16 used as the status LED via a free PIO1 state machine (the DCO uses PIO0).
2. Make the button pin configurable (`BTN_PIN`, e.g. 20 for a carrier with the shifted pad).
3. Use `PICO_BOARD=waveshare_rp2350_zero` once (1) is done.

## 4. Flashing

1. Hold **BOOT**, tap **RUN**, then release BOOT. Or hold BOOT while plugging in USB-C. A drive named `RP2350` appears.
2. Copy the UF2 onto it. The board reboots by itself.
3. Open the USB serial port (any baud). A new board has no valid settings, so the settings menu opens by itself and waits for the terminal. Later, hold the GP21 button at power-up to get the menu.

## 5. Staged test plan

Do stages 1-5 on the bench into a **50 Ω dummy load**, with the low-pass filter in place, and with **30-40 dB of attenuation** before the SDR.

| # | Stage | Do | Pass | Fail → |
|---|---|---|---|---|
| 1 | USB serial | Flash, open the port | Banner `N2GF build: sysclk 288 MHz, DCO fine (dco3) engine, max RF 36000000 Hz` | No banner, or reboot loop: wrong UF2, or overclock/flash trouble. Try `pico2_10m_288MHz_classic_FALLBACK.uf2` and send the log |
| 2 | Settings | Enter `CALLSIGN`, `LOCATOR`, `MODE WSPR`, `BAND 10`, `GPS ON`, `GPSCAL ON`, `RFPIN 6`, `POWER`, `SLOTSKIP`, `CALPPM 0`, then `EXIT` | The printout shows those values, including `GPSCAL:On` | — |
| 3 | Clock and flash stability | Power-cycle 3 times; change one setting, save and reboot; leave it running for 1 h | Settings survive the reboots and the save; no spontaneous reboots (the banner doesn't reappear); `DCO FIFO underrun during Tx: no` after each Tx | Crash right at a settings save: the flash size or timing problem in §2. Any underrun = YES: report it |
| 4 | GPS | Antenna outdoors or at a window | `Init GPS`, `GPS PPS received`, then `PPS ok:` counting up and `bad NMEA:` near 0. After about 33 s of PPS, an end-of-Tx line shows `Freq corr: <n> ppb (GPS locked)` with `win-rej:0`. With `GPSLOCATION ON`, the grid is correct | Stuck at `Wait for GPS PPS pulses`: check the PPS and GP2 wiring and the GPS fix. `filtered:` climbing fast: noise on PPS |
| 5a | Carrier | SDR at 28.1261 MHz, under 1 Hz/bin | 4 tones about 1.46 Hz apart, about 6 Hz wide | No signal: RF pin/RFPIN, LPF |
| 5b | Frequency error | Compare the measured tone with 28,126,100 Hz + offset, after 10 min of warm-up | With GPS locked: within **±2 Hz**. With GPSCAL OFF, compute CALPPM = −ppb/1000 from the log, or from the measured error, and re-check | More than 5 Hz off when locked: send the log |
| 5c | Harmonics and spurs | Look at 56.25, 84.38 and 140.6 MHz, sweep 1-30 MHz, and look within ±1 MHz of the carrier | Every spur and harmonic **≥ 43 dB below the carrier** (FCC §97.307(e), transmitters below 30 MHz) | Expect it to fail close in: the model predicts −25 dBc around 20.8 MHz and 27.54 MHz. Don't go on air until it passes, or add filtering or an Si5351 |
| 6 | PPS loss | During a Tx, unplug the GPS antenna, or pull the PPS wire (some modules keep pulsing without a fix) | `GPS PPS missing - holdover tick N` lines, the Tx finishes on time, the end-of-Tx line shows `(GPS held, PPS lost)` with ppb unchanged, and **no reboot** (no new banner). After reconnecting, back to `GPS locked` within about 1 min | A banner mid-Tx means a watchdog reset: send the log |
| 7 | On air | Antenna through the LPF, only if 5c passes | Within 24-48 h, wsprnet.org (Database → Spots, call N2GF, 10 m) or wspr.rocks shows spots at **28.126000-28.126200 MHz**, drift 0 or ±1 | No spots during known 10 m openings: check 5b. If spots show drift, send them |

## 6. What to send back
- The serial log from boot through 2 full transmissions, including the settings printout and every `WSPR>` line (`Start TX.`, `End Tx.`, `DCO FIFO underrun…`, `Freq corr…`).
- The stage 6 log, covering before, during and after unplugging.
- `picotool info -a` output, taken in BOOT mode.
- An SDR screenshot of the tones (stage 5a/b) and of the 1-150 MHz sweep (5c), with the attenuation noted.
- Bottom-pad continuity results (§2): which carrier pads are GND, GP21 and GP25.
- wsprnet spot links or screenshots.
