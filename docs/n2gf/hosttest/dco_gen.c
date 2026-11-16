/* Emit the sequence of half-period lengths (sysclk cycles, uint8) the GPIO sees.
 * mode 0 = original dco2.pio + PioDCOWorker2 (one 32-bit y for 4 half periods, y+4 cycles each)
 * mode 1 = new dco3.pio + worker (one 32-bit word = 4 x 8-bit counts, x+2 cycles each,
 *          total for the 4 halves dithered to 1 cycle, spread evenly)
 * usage: dco_gen mode sys_hz f_millihz nhalf > out.u8                                 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    int mode = atoi(argv[1]); double sys = atof(argv[2]); double f = atof(argv[3]) / 1000.0;
    long nh = atol(argv[4]);
    uint8_t *o = malloc(nh + 8); long k = 0;
    if (mode == 0) {
        int64_t den = (int64_t)(2000.0 * f);  /* fine enough for spur modelling */
        int32_t cpp = (int32_t)((((int64_t)sys) * (1LL << 24) * 1000LL + den / 2) / den);
        uint32_t reg = (uint32_t)(cpp - (4 << 24)); int32_t err = 0;
        while (k < nh) { uint32_t wc = (reg - err) >> 24; err += (wc << 24) - reg;
            for (int i = 0; i < 4; i++) o[k++] = wc + 4; }
    } else {
        /* total cycles for 4 half periods, scaled 2^24: 4*sys/(2f) */
        uint32_t reg4 = (uint32_t)(4.0 * sys / (2.0 * f) * 16777216.0 + 0.5) - (8u << 24);
        static const uint32_t spread[4] = {0x00000000, 0x00000001, 0x00010001, 0x00010101};
        int32_t err = 0;
        while (k < nh) { uint32_t w = (reg4 - err) >> 24; err += (w << 24) - reg4;
            uint32_t word = (w >> 2) * 0x01010101u + spread[w & 3];
            for (int i = 0; i < 4; i++) o[k++] = ((word >> (8 * i)) & 0xff) + 2; }
    }
    fwrite(o, 1, nh, stdout); return 0;
}
