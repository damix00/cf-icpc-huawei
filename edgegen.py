"""Generate adversarial edge-case instances that gen.cpp never produces.

gen.cpp samples a comfortable middle of the constraint space.  The real tests may sit on the
corners: a single request, one layer, one remote, the largest and smallest legal table values,
tables whose rows are unsorted and full of holes, transfer costs at both extremes.  A protocol
violation on any of them is a zero, so they are worth far more than a few points of tp.

  py edgegen.py edge      # writes edge/*.txt
"""
import os, sys, random

OUT = sys.argv[1] if len(sys.argv) > 1 else "edge"
os.makedirs(OUT, exist_ok=True)


def write(name, K, S, lat, bw, bpt, layers, rows, reqs, wtp=0.5,
          slo1=1000.0, slo2=1000.0, tpub=1.0, tpbase=0.0, distbase=0.0):
    with open(os.path.join(OUT, name + ".txt"), "w") as f:
        f.write("%d %.9f %.9f %.9f %.9f %d\n" % (K, S, lat, bw, bpt, layers))
        f.write("%.9f %.9f %.9f %.9f %.9f %.9f %.9f\n"
                % (slo1, slo2, tpub, tpbase, distbase, wtp, 1.0 - wtp))
        f.write("%d\n" % len(rows))
        for r in rows:
            f.write("%d " % r[0] + " ".join("%.9f" % v for v in r[1:]) + "\n")
        f.write("%d\n" % len(reqs))
        for a, li, lo in reqs:
            f.write("%.9f %d %d\n" % (a, li, lo))


FLAT = [(1, 1, 1, 1, 1, 1, 1), (4096, 1, 1, 1, 1, 1, 1)]
# every column has at least one entry, but most entries are missing (-1)
HOLED = [(1, 0.5, 2.0, 0.5, -1, -1, -1), (7, -1, -1, -1, 0.25, 3.0, 0.25),
         (4096, 2.0, 900.0, 2.0, -1, 12.0, -1), (33, -1, -1, -1, -1, -1, 0.5)]
# rows deliberately out of order, non power-of-two sizes
UNSORTED = [(4096, 3, 500, 3, 2, 40, 2), (7, 1, 5, 1, 0.5, 10, 0.5),
            (933, 2, 200, 2, 1.5, 30, 1.5), (1, 0.5, 2, 0.5, 0.25, 8, 0.25)]
BIGSPAN = [(1, 0.001, 0.001, 0.001, 0.001, 0.001, 0.001),
           (4096, 10000, 10000, 10000, 10000, 10000, 10000)]

cases = {
    # minimal everything
    "min_1req":        dict(K=1, S=1, lat=0.001, bw=100, bpt=1, layers=1, rows=FLAT,
                            reqs=[(0.0, 1, 1)]),
    # a single request but every mechanic at maximum
    "max_1req":        dict(K=8, S=10, lat=50, bw=0.001, bpt=10**6, layers=64, rows=FLAT,
                            reqs=[(0.0, 4096, 512)]),
    # many remotes, one request: 7 remotes must stay unused and legal
    "k8_1req":         dict(K=8, S=5, lat=1, bw=1, bpt=1000, layers=32, rows=FLAT,
                            reqs=[(0.0, 100, 5)]),
    # every request produces exactly one token -> no TPOT gaps at all
    "all_lout1":       dict(K=4, S=3, lat=0.5, bw=5, bpt=5000, layers=8, rows=FLAT,
                            reqs=[(0.0, 1 + i % 500, 1) for i in range(300)]),
    # one layer disables prefill splitting entirely
    "layers1_burst":   dict(K=6, S=8, lat=2, bw=2, bpt=20000, layers=1, rows=FLAT,
                            reqs=[(0.0, 4096, 8) for _ in range(200)]),
    # maximum layers with a single request: the piece machinery at its extreme
    "layers64_1req":   dict(K=2, S=1, lat=0.01, bw=50, bpt=10, layers=64, rows=FLAT,
                            reqs=[(0.0, 4096, 3)]),
    # arrivals spread over the whole legal horizon: long idle gaps between frames
    "sparse_arrivals": dict(K=3, S=4, lat=5, bw=1, bpt=1000, layers=4, rows=FLAT,
                            reqs=[(i * 5e7, 1 + i * 37 % 4096, 1 + i % 9) for i in range(20)]),
    # table with holes in most columns
    "holed_table":     dict(K=4, S=2, lat=0.7, bw=3, bpt=2500, layers=16, rows=HOLED,
                            reqs=[(0.0, 1 + (i * 91) % 4096, 1 + i % 40) for i in range(150)]),
    # unsorted, non-power-of-two rows
    "unsorted_table":  dict(K=5, S=6, lat=3, bw=0.5, bpt=40000, layers=12, rows=UNSORTED,
                            reqs=[(0.0, 1 + (i * 271) % 4096, 1 + i % 25) for i in range(180)]),
    # table values spanning the full legal range
    "extreme_table":   dict(K=2, S=10, lat=25, bw=0.01, bpt=500000, layers=2, rows=BIGSPAN,
                            reqs=[(0.0, 1 + i % 3, 1 + i % 4) for i in range(30)]),
    # link cost as slow as the constraints allow
    "slow_link":       dict(K=8, S=1, lat=50, bw=0.001, bpt=10**6, layers=1, rows=FLAT,
                            reqs=[(0.0, 1, 2) for _ in range(40)]),
    # link effectively free: compute is everything
    "fast_link":       dict(K=8, S=10, lat=0.001, bw=100, bpt=1, layers=64, rows=FLAT,
                            reqs=[(0.0, 4096, 6) for _ in range(120)]),
    # the largest legal population, one token each
    "r2000_lout1":     dict(K=8, S=1, lat=0.1, bw=10, bpt=100, layers=1, rows=FLAT,
                            reqs=[(0.0, 1 + i % 4096, 1) for i in range(2000)]),
    # the token budget concentrated in very few requests
    "few_long":        dict(K=8, S=5, lat=10, bw=1, bpt=1000, layers=20, rows=FLAT,
                            reqs=[(0.0, 2000, 512) for _ in range(8)]),
    # all identical arrival timestamps, maximum count
    "burst2000":       dict(K=8, S=2, lat=0.05, bw=20, bpt=800, layers=6, rows=FLAT,
                            reqs=[(0.0, 1 + (i * 13) % 4096, 1 + i % 100) for i in range(2000)]),
    # throughput-only and waiting-only scoring
    "wtp_zero":        dict(K=4, S=3, lat=1, bw=4, bpt=3000, layers=8, rows=FLAT, wtp=0.0,
                            reqs=[(0.0, 1 + i % 900, 1 + i % 30) for i in range(200)]),
    "wtp_one":         dict(K=4, S=3, lat=1, bw=4, bpt=3000, layers=8, rows=FLAT, wtp=1.0,
                            reqs=[(0.0, 1 + i % 900, 1 + i % 30) for i in range(200)]),
}

rng = random.Random(20260815)
# a handful of fully random corner draws
for n in range(6):
    K = rng.choice([1, 1, 2, 8])
    cases["rand%d" % n] = dict(
        K=K, S=rng.choice([1.0, 10.0, 5.5]), lat=rng.choice([0.001, 50.0, 7.0]),
        bw=rng.choice([0.001, 100.0, 1.0]), bpt=rng.choice([1, 10**6, 4096]),
        layers=rng.choice([1, 64, 9]), rows=rng.choice([FLAT, HOLED, UNSORTED, BIGSPAN]),
        wtp=rng.choice([0.0, 0.5, 1.0]),
        reqs=[(0.0 if rng.random() < .5 else rng.uniform(0, 1e6),
               rng.choice([1, 4096, rng.randint(1, 4096)]),
               rng.choice([1, 512, rng.randint(1, 64)])) for _ in range(rng.randint(1, 120))])

for name, kw in cases.items():
    write(name, **kw)
print("wrote %d edge cases to %s/" % (len(cases), OUT))
