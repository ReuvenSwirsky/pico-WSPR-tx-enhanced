import numpy as np, sys
from multiprocessing import Pool
from spur_sim2 import spec
def metric(args):
    mode, s = args; res = []
    for f in (28126100-80, 28126100, 28126100+1.4648*3, 28126100+80):
        fr, db = spec(mode, s*1e6, f, N=1 << 22); df = fr[1]
        car = np.argmin(abs(fr - f)); ex = int(1500 / df)
        def worst(lo, hi):
            idx = np.where((fr > lo) & (fr < hi))[0]; idx = idx[(idx < car - ex) | (idx > car + ex)]
            k = idx[np.argmax(db[idx])]; return db[k], fr[k]
        res.append((worst(1e6, 31e6), worst(31e6, 50e6)))
    inb = max(r[0][0] for r in res); hi = max(r[1][0] for r in res)
    w = max(res, key=lambda r: r[0][0])[0]
    return s, mode, inb, w[1], hi
if __name__ == "__main__":
    mode = int(sys.argv[1]); clocks = range(int(sys.argv[2]), int(sys.argv[3]) + 1, int(sys.argv[4]))
    with Pool(8) as p:
        for s, m, inb, wf, hi in p.map(metric, [(mode, c) for c in clocks]):
            print(f"mode {m} sys {s} MHz: worst 1-31 MHz {inb:6.1f} dBc @ {wf/1e6:8.4f} | worst 31-50 MHz {hi:6.1f} dBc", flush=True)
