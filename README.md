# pico-WSPR-tx
Modified version of WSPR beacon for Raspberry Pi Pico, based on pico-hf-oscillator PLL DCO library.
The GPS reference is used to compensate Pico's clock drift. GPS is also used to schedule WSPR transmissions.

![image](https://github.com/RPiks/pico-WSPR-tx/assets/47501785/3f835a9d-fa42-4eb8-ba93-ea72033d9e62)
The example of this WSPR beacon (QRPX power level) reception. Last max QRB is ~3400 km on 40m band.

![image](https://github.com/RPiks/pico-WSPR-tx/assets/47501785/a86280b9-71cb-4bb2-8b3c-0e33d2499aca)
High spectrum quality and less than 1Hz frequency drift.

# *NO* additional hardware
The WSPR beacon provides the output signal on the GPIO pin of Raspberry Pi Pico. No externall PLL, analog oscillators!
You should only provide a lowpass filter of appropriate frequency if you want to use this module extensively. 
The power transmitted using GPIO pin is sufficient only when using full-size dipole as antenna. If there is no such option available, you need to boost the signal using simple 1 transistor amplifier.

# For what?
This is an experimental project of amateur radio hobby and it is devised by me in order to experiment with QRP narrowband digital modes.
I am licensed radio amateur who is keen on experiments in the area of the digital modes on HF. 
My QRZ page is https://www.qrz.com/db/R2BDY

# Quick-start
1. Install Raspberry Pi Pico SDK. Configure environment variables. Test whether it is built successfully.

2. clone this repo
3. Open the repo folder in VSCode
4. When prompted, import folder as a Pico project
5. Compile and 'Run' the project to upload into the Pico.
6. Use a serial terminal to enter your callsign and location
7. Default band is 40m


Cheers,
Roman Piksaykin, amateur radio callsign R2BDY
https://www.qrz.com/db/R2BDY
piksaykin@gmail.com


Enhancements and modifications by VK3KYY December 2025

1. Add external button press to manually start Tx
2. Add settings configuation via serial terminal to allow callsign, location, band and other things to be set without needing to compile from source
3. Fixed problem of Tx not ending until the start of the next 'slot'
4. Fixed DT problem caused by modulation control ISR effectively free running
5. Changed memory allocation to be static as dymanic memory allocation is generally avoided in firmware applications
6. Automatic detection of GPS module if attached. Otherwise default to external button press
7. Hold external button when connecting Pico to USB forces settings entry so changes can be made
8. Overclocking is 200MHz on the original Pico (RP2040), as the original 270MHz would not run on many Pico boards. This limits the highest usable band to 15m (the PIO oscillator needs at least 4 clocks per RF half cycle, so max RF = sysclock / 8 = 25MHz). On the Pico 2 (RP2350) the system clock is 300MHz, which covers 10m with margin (see "Pico 2 / 10m" below).
9. Add frequency hoping option to settings
10. Added frequency calibration to settings for use when not connected to GPS that provides frequency calibration
11. Fixed bug with initial part of transmission was sending the last data 'symbol' from the last transmission
12. Improved LED flashing functionality. Slow very slow flash, waiting for external button to start. slow flash in passive slot, fast flash transmitting
13. Add setting to allow initial freq from the center of the WSPR range to be set.
14. Now works on both the original Pico (1) and also the Pico 2. Pico 2 works in both ARM and RISC-V mode
15. Added functionality to send 6 figure Maindenhead locator
16. Added functionality to use GPS to set the Maidenhead locator.
17. Added CW Beacon functionality
18. Added Slow Morse functionality which sends 5 figure letter / number groups
19. Pico 2 (RP2350) now runs at 300MHz so that 12m and 10m work. No pinout changes - it is a drop-in replacement for a Pico 1 build.
20. Re-enabled the GPS PPS frequency calibration (it had been disabled because it produced huge bogus values). The bug was a signed/unsigned mix-up in GPStime.c: when the Pico crystal runs slow the (negative) error was divided as an unsigned 64-bit number and came out as ~+5.8e17 ppb, which then overflowed the correction multiply in piodco.c. Both are fixed and the correction is now bounded. It can be turned off with GPS_PPS_FREQ_CORRECTION in defines.h.
21. The firmware now prints the system clock, the oscillator frequency limit and the Tx frequency at boot, and warns if the selected band is above the limit.


Still to do. 

1. Multi-band option
2. Implement FT8
3. Implement APRS

KNOWN BUGS / LIMITATIONS
1. On the original Pico (RP2040) at 200MHz, bands above 20m are marginal and were observed to fail when the GPS is in use. This is NOT caused by the frequency correction math (that was already compiled out when it was observed). The cause is timing: core 1 has to feed the PIO one word every 2 RF cycles, which at 200MHz is 22 CPU cycles on 17m and 19 on 15m, while the feed loop itself needs about 17. Any extra bus activity from core 0 (GPS UART interrupts at ~1kHz, NMEA parsing, debug printing every second) is enough to starve the PIO FIFO and the output goes off frequency. On the Pico 2 at 300MHz the budget on 10m is 21 cycles against a ~13 cycle loop, so there is real margin. If you need 17m/15m with GPS on an RP2040, try PLL_SYS_MHZ_OVERCLOCK_270MHZ in defines.h (not all boards will run it).
2. 12m and 10m are not possible on the RP2040 at 200MHz (above sysclock / 8). The boot message will warn you.


Pico 2 / 10m

The RP2350 build runs the system clock at 300MHz (core voltage 1.20V). Because the QSPI flash clock is derived from the system clock, the flash divider is changed from 2 to 3 before the clock is raised so the flash stays at 100MHz (within the W25Q32 133MHz limit). The peripheral clock is left at 48MHz. All of this is selected automatically by the PICO_RP2350 define; the RP2040 build is unchanged.

To build for a Pico 2, either use the Pico VS Code extension's "Switch Board" and select pico2, or from a command line:

    cmake -S . -B build-pico2 -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=pico2
    ninja -C build-pico2

then flash build-pico2/pico-wspr-tx-enhanced.uf2. Set BAND 10 in the settings.

If a particular board does not come up at 300MHz, lower PLL_SYS_MHZ_OVERCLOCK_300MHZ in defines.h (250MHz still allows 10m, but with less margin), or try VREG_VOLTAGE_1_25 in init.c.


Quickstart

1. Install the firmware
2. Open a serial monitor to the USB Serial from the Pico
3. Enter your callsign   e.g.  CALLSIGN VK3KYY  
4. Enter your Maidenhead locator e.g. LOCATOR QF12AB   .   By default only the first 4 characters will be used unless sending 6 characters is enabled
5. Choose the MODE.   WSPR , CW , SLOWMORSE .  Default will be WSPR.  FT8 and APRS , and other modes may be added at a later date.
6. Type Exit  or Reboot or power cycle the board

By default the firmware will check for 2 seconds if a GPS is connected. If GPS is set to OFF it won't check and GPS won't be used even if connected.
If a GPS is not connnected, or its disabled in the settings, the firmware will wait for a button attached to the board to be pressed.


GPS can be connected to
GPS_PPS  to PIN GPIO 2
GPS serial data to UART 0. On the UART Rx in on the Pico needs to be connected (GPIO 4)
GPS_PPS is necessary for GPS operation as it is used for accurate time keeping and also to calibrate the Tx freq and compensate for drift duing Tx.


RF Pin is configurable in the settings, and defaults to GPIO 6. I don't know if all GPIO pins work.

When not using GPS. The inaccuracy of the master clock crystal on the Pico board can be compensated for by using the CALPPM setting.
This value can be positive or negative and is the Parts Per Million amount of correction needed.
By default the WSPR beacon will transmit in the middle of the WSPR band, so using a receiver to check whether the transmission is on frequency, the PPM value can be calculated and entered.

A button need to be connected when not using a GPS for WSPR mode. Pressing the button starts the WSPR transmission
The button needs to be connected between GPIO 21 and Vcc (3.3V). An internal pulldown is used, so there is no need for any other external components
The button pin is not currently configurable

Holding the Button Pin when powering the Pico will force entry into the Settings


IMPORTANT

The RF output from the Pico is basically a square wave and hence contains lots of harmonics. 
A low pass filter should be connected between the RF pin and an antenna to prevent spurious unintended harmonics etc being transmitted.

Also. The frequency generation method seems to produce multiple artifacts aka Spur Frequencies, at regular intervals around the intended output frequency.
The frequency generation method in the original code is not documented, but is believed to be similar to that used in the Si5351
See this web page where the Si5351 frequency generation method is reverse engineered
https://www.pa3fwm.nl/technotes/tn42b-si5351-analysis.html

Hence this is not a pure continouos square wave of the output frequecy, it's a combination of 2 different square wave frequences which are transmitted for a minimum of 2 cycles.


Enjoy


73

Roger
VK3KYY

