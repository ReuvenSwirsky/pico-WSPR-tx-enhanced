"""Spectrum model of the pico-hf-oscillator DCO (dco2.pio + PioDCOWorker2).
Builds the GPIO square wave at sysclk resolution and FFTs it.
usage: python spur_sim.py  (prints worst spur levels vs carrier for several sysclk)"""
import numpy as np, sys

def edges(sys_hz, f_hz, nwords, shift=24):
    hp = sys_hz / (2.0 * f_hz)                                  # half period in cycles
    reg = int(round(hp * (1 << shift))) - (4 << shift)          # si32precise_cycles
    m = (1 << 32) - 1
    err = 0; ys = np.empty(nwords, dtype=np.int64)
    for n in range(nwords):
        u = (reg - err) & m
        wc = u >> shift
        err = (err + (wc << shift) - reg) & m
        if err >= 1 << 31: err -= 1 << 32
        ys[n] = wc
    return np.repeat(ys + 4, 4)                                 # 4 half periods per word

def spectrum(sys_hz, f_hz, N=1 << 22, shift=24):
    nwords = int(N / (4 * sys_hz / (2 * f_hz))) + 8
    hps = edges(sys_hz, f_hz, nwords, shift)
    level = np.zeros(int(hps.sum()) + 1, dtype=np.float32)
    t = np.concatenate(([0], np.cumsum(hps)))
    for i in range(0, len(t) - 1, 2):
        level[t[i]:t[i + 1]] = 1.0
    x = level[:N] - 0.5
    w = np.blackman(N)
    X = np.abs(np.fft.rfft(x * w)); X /= X.max()
    fr = np.fft.rfftfreq(N, 1.0 / sys_hz)
    return fr, 20 * np.log10(X + 1e-12)

def report(sys_mhz, f_hz=28126100, shift=24):
    sys_hz = sys_mhz * 1e6
    fr, db = spectrum(sys_hz, f_hz, shift=shift)
    df = fr[1]
    car = np.argmin(abs(fr - f_hz))
    excl = int(3000 / df)                                       # +-3 kHz around carrier = main lobe
    def worst(lo, hi):
        sel = (fr > lo) & (fr < hi); idx = np.where(sel)[0]
        idx = idx[(idx < car - excl) | (idx > car + excl)]
        k = idx[np.argmax(db[idx])]; return db[k], fr[k]
    a = worst(f_hz - 100e3, f_hz + 100e3)
    b = worst(f_hz - 3e6, f_hz + 3e6)
    c = worst(0, 0.8 * f_hz)                                     # below carrier (not removed by LPF)
    d = worst(1.2 * f_hz, sys_hz / 2)                            # above (LPF removes most)
    print(f"sysclk {sys_mhz:3d} MHz shift {shift}: within +-100 kHz {a[0]:6.1f} dBc @ {a[1]/1e6:.4f} MHz | "
          f"+-3 MHz {b[0]:6.1f} dBc @ {b[1]/1e6:.4f} | below 0.8f {c[0]:6.1f} dBc @ {c[1]/1e6:.3f} | "
          f"above 1.2f {d[0]:6.1f} dBc @ {d[1]/1e6:.3f} MHz", flush=True)

if __name__ == "__main__":
    for s in [int(a) for a in sys.argv[1:]] or [200, 276, 300, 320]:
        report(s, 28126100 if s > 230 else 24926100)
