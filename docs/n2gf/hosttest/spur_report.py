from spur_sim2 import spec
import numpy as np
def rep(label, mode, sys_mhz, f):
    fr, db = spec(mode, sys_mhz*1e6, f, N=1 << 23); df = fr[1]; car = np.argmin(abs(fr-f)); ex = int(1500/df)
    def worst(lo, hi):
        idx = np.where((fr > lo) & (fr < hi))[0]; idx = idx[(idx < car-ex) | (idx > car+ex)]
        k = idx[np.argmax(db[idx])]; return f"{db[k]:6.1f} dBc @ {fr[k]/1e6:8.4f} MHz"
    print(f"{label:44s} | +-1.5..50 kHz {worst(f-50e3,f+50e3)} | +-1 MHz {worst(f-1e6,f+1e6)} | 1 MHz..0.9f {worst(1e6,0.9*f)} | 1.1f..2f {worst(1.1*f,2*f)}")
print("Idealised model: GPIO edges quantised to sysclk, no PLL jitter / pad effects. dBc vs carrier, Blackman window, 8M-sample FFT.")
rep("original firmware, Pico 1 @200 MHz, 20 m", 0, 200, 14097100)
rep("original engine @200 MHz, 12 m", 0, 200, 24926100)
rep("classic engine @288 MHz, 10 m (fallback uf2)", 0, 288, 28126100)
rep("fine engine @288 MHz, 10 m (main uf2)", 1, 288, 28126100)
rep("fine engine @288 MHz, 20 m", 1, 288, 14097100)
