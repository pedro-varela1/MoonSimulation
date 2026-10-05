"""Reference implementation of the spatial split generator (data/05_splits.md §1).
Produces the block->partition table, a 0.1-deg raster and the area shares. Deterministic given `seed`."""
import json
import numpy as np

BANDS = [(-80, -60), (-60, -40), (-40, -20), (-20, 0), (0, 20), (20, 40), (40, 60), (60, 80)]
N_LON = {20: 2, 40: 3, 60: 4, 80: 6}           # by poleward |lat| of the band
PICK = {20: (4, 2), 40: (2, 1), 60: (2, 1), 80: (1, 1)}   # (test, val) clusters per band
CODE = {"train": 0, "val": 1, "test": 2}


def make_regions(seed=2026):
    rng = np.random.default_rng(seed)
    part = {}                                       # (lat0, lon0) of 10-deg block -> partition
    clusters_out = []
    for lat0, lat1 in BANDS:
        pole = max(abs(lat0), abs(lat1))
        n_lon = N_LON[pole]
        n_cl = 36 // n_lon
        clusters = [(lat0, lat1, k * 10 * n_lon - 180, (k + 1) * 10 * n_lon - 180) for k in range(n_cl)]
        near = [c for c in clusters if abs((c[2] + c[3]) / 2) < 90]
        far = [c for c in clusters if abs((c[2] + c[3]) / 2) >= 90]
        rng.shuffle(near); rng.shuffle(far)
        n_test, n_val = PICK[pole]
        order = []                                   # alternate near/far, starting side random
        start_near = rng.random() < 0.5
        a, b = (near, far) if start_near else (far, near)
        for i in range(max(len(a), len(b))):
            if i < len(a): order.append(a[i])
            if i < len(b): order.append(b[i])
        test, val = order[:n_test], order[n_test:n_test + n_val]
        for c in clusters:
            p = "test" if c in test else "val" if c in val else "train"
            clusters_out.append(dict(lat=[c[0], c[1]], lon=[c[2], c[3]], partition=p))
            for la in range(c[0], c[1], 10):
                for lo in range(c[2], c[3], 10):
                    part[(la, lo)] = p
    return part, clusters_out


def raster(part, res=0.1):
    lat = np.arange(-90 + res / 2, 90, res)
    lon = np.arange(-180 + res / 2, 180, res)
    R = np.zeros((lat.size, lon.size), np.uint8)
    for i, la in enumerate(lat):
        if la <= -80: R[i, :] = CODE["test"]; continue     # south cap = test
        if la >= 80: R[i, :] = CODE["train"]; continue     # north cap = train
        la0 = int(np.floor(la / 10) * 10)
        for j0 in range(36):
            lo0 = -180 + 10 * j0
            R[i, int(round((lo0 + 180) / res)):int(round((lo0 + 190) / res))] = CODE[part[(la0, lo0)]]
    return R, lat, lon


def area_shares(R, lat):
    w = np.cos(np.radians(lat))[:, None] * np.ones_like(R, float)
    return {k: float((w * (R == v)).sum() / w.sum()) for k, v in CODE.items()}


if __name__ == "__main__":
    # LINNA-2 generator: python regions.py <output_dir> [seed] -> <output_dir>/splits/regions.json and regions_raster.npy (once, then frozen)
    import os
    import sys
    out = os.path.join(sys.argv[1], "splits")
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 2026
    if os.path.exists(os.path.join(out, "regions.json")):
        sys.exit(f"{out}/regions.json exists: the regions are frozen (delete it to draw them again)")
    os.makedirs(out, exist_ok=True)
    part, clusters = make_regions(seed)
    R, lat, lon = raster(part)
    shares = area_shares(R, lat)
    print({k: round(v, 4) for k, v in shares.items()})
    json.dump(dict(seed=seed, clusters=clusters, caps={"south": "test", "north": "train"}, area_shares=shares,
                   raster="regions_raster.npy (uint8, rows from -90 deg latitude, columns from -180 deg longitude, 0.1 deg; 0 train, 1 val, 2 test)"),
              open(os.path.join(out, "regions.json"), "w"), indent=1)
    np.save(os.path.join(out, "regions_raster.npy"), R)
