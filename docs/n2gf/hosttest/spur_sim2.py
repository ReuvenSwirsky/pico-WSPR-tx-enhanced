import numpy as np, subprocess, sys
def spec(mode, sys_hz, f_hz, N=1 << 23):
    nh = int(N / (sys_hz / (2 * f_hz))) + 16
    hp = np.frombuffer(subprocess.run(["./dco_gen", str(mode), str(sys_hz), str(f_hz * 1000), str(nh)],
                       capture_output=True).stdout, dtype=np.uint8).astype(np.int64)
    lv = np.repeat(np.tile([1.0, 0.0], len(hp) // 2 + 1)[:len(hp)], hp)[:N].astype(np.float32) - 0.5
    X = np.abs(np.fft.rfft(lv * np.blackman(N).astype(np.float32))); X /= X.max()
    return np.fft.rfftfreq(N, 1 / sys_hz), 20 * np.log10(X + 1e-12)
def report(mode, sys_mhz, f_hz=28126100):
    fr, db = spec(mode, sys_mhz * 1e6, f_hz); df = fr[1]
    car = np.argmin(abs(fr - f_hz)); peak = fr[np.argmax(db)]
    ex = int(1000 / df)
    def worst(lo, hi):
        idx = np.where((fr > lo) & (fr < hi))[0]; idx = idx[(idx < car - ex) | (idx > car + ex)]
        k = idx[np.argmax(db[idx])]; return f"{db[k]:6.1f} dBc @{fr[k]/1e6:9.4f}"
    print(f"mode {mode} sys {sys_mhz:3d} MHz (peak at {peak/1e6:.5f}): +-1..20 kHz {worst(f_hz-20e3,f_hz+20e3)} | "
          f"+-1 MHz {worst(f_hz-1e6,f_hz+1e6)} | 1-27 MHz {worst(1e6,27e6)} | 30-60 MHz {worst(30e6,60e6)}", flush=True)
if __name__ == "__main__":
    for m in (0, 1):
        for s in map(int, sys.argv[1:]): report(m, s)
