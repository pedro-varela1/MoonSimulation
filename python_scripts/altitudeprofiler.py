"""Altitude profile of an STK orbit report: altitude above the lunar relief and above the 1737.4 km reference sphere.

usage: python altitudeprofiler.py <orbit.txt>

Writes outputs/<orbit>_altitudeprofile.csv and .png next to this script. The terrain is the simulator's: SLDEM2015
512 ppd up to 60 deg of latitude and the LOLA 60 m polar stereographic DEMs above, with the data paths of config.ini.
As in the simulator, the report's latitude and longitude are MOON_PA and are rotated to MOON_ME, the frame of the DEMs.
Needs numpy, pandas, matplotlib and spiceypy.
"""
import re
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import spiceypy as sp

R = 1737.4                        # reference sphere, km
SLDEM_H, SLDEM_W = 15360, 23040   # one SLDEM file: 30 deg x 45 deg at 512 ppd
POLAR_N, POLAR_C, POLAR_S = 31040, 15520, 0.06  # polar DEM: size, pole position and pixel size (km)
GAP, RUN = 32, 1024               # rows read in one go: runs of needed rows less than GAP apart, at most RUN long
MONTHS = {m: f"{i + 1:02d}" for i, m in enumerate("Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec".split())}
HERE = Path(__file__).resolve().parent


def config():
    """config.ini of the simulator; a comment starts at a '#' that begins the line or follows a blank"""
    cfg = {}
    for line in (HERE.parent / "config.ini").read_text(encoding="utf-8").splitlines():
        line = re.split(r"(?:^|(?<=\s))#", line, maxsplit=1)[0]
        if "=" in line:
            key, value = line.split("=", 1)
            cfg[key.strip()] = value.strip()
    return cfg


def read_orbit(path):
    """rows after the '----' line of an STK report: 'd Mon yyyy hh:mm:ss.sss lat lon alt'"""
    with open(path) as f:
        header = next(i for i, line in enumerate(f) if line.strip().startswith("----")) + 1
    df = pd.read_csv(path, sep=r"\s+", skiprows=header, header=None, names=["d", "mon", "y", "hms", "lat", "lon", "alt"],
                     dtype=str, on_bad_lines="skip")
    df = df[df.mon.isin(MONTHS.keys())]
    for k in ("lat", "lon", "alt"):
        df[k] = pd.to_numeric(df[k], errors="coerce")
    df = df.dropna()
    df["utc"] = df.y + "-" + df.mon.map(MONTHS) + "-" + df.d.str.zfill(2) + "T" + df.hms
    return df.reset_index(drop=True)


def pick(path, dtype, width, r, c):
    """values of a raw row-major image at rows r and columns c, reading runs of nearby rows at once"""
    out = np.empty(r.size)
    order = np.argsort(r, kind="stable")
    r, c = r[order], c[order]
    rows = np.unique(r)
    starts = [rows[0]]
    for a, b in zip(rows[:-1], rows[1:]):
        if b - a > GAP or b - starts[-1] >= RUN:
            starts.append(b)
    ends = [rows[np.searchsorted(rows, s) - 1] + 1 for s in starts[1:]] + [rows[-1] + 1]
    with open(path, "rb") as f:
        for a, b in zip(starts, ends):
            f.seek(int(a) * width * np.dtype(dtype).itemsize)
            block = np.fromfile(f, dtype, int(b - a) * width).reshape(-1, width)
            i, j = np.searchsorted(r, [a, b])
            out[order[i:j]] = block[r[i:j] - a, c[i:j]]
    return out


def bilinear(Y, X, fetch):
    """bilinear interpolation at pixel coordinates (Y, X), pixel centers at integers; fetch(rows, cols) gives the pixels"""
    y0, x0 = np.floor(Y).astype(np.int64), np.floor(X).astype(np.int64)
    fy, fx = Y - y0, X - x0
    v = fetch(np.concatenate([y0, y0, y0 + 1, y0 + 1]), np.concatenate([x0, x0 + 1, x0, x0 + 1])).reshape(4, -1)
    return (v[0] * (1 - fx) + v[1] * fx) * (1 - fy) + (v[2] * (1 - fx) + v[3] * fx) * fy


def sldem(lat, lon, root):
    """SLDEM2015 height (km) for |lat| <= 60 deg"""
    names = ["30n_60n", "00n_30n", "30s_00s", "60s_30s"]

    def fetch(r, c):
        c = c % (8 * SLDEM_W)
        f = r // SLDEM_H * 8 + c // SLDEM_W
        v = np.empty(r.size)
        for k in np.unique(f):
            m = f == k
            path = root / f"sldem2015_512_{names[k // 8]}_{k % 8 * 45:03d}_{k % 8 * 45 + 45:03d}_float.img"
            print(f"  {path.name}: {m.sum() // 4} points", flush=True)
            v[m] = pick(path, "<f4", SLDEM_W, r[m] % SLDEM_H, c[m] % SLDEM_W)
        return v

    return bilinear(np.clip((60 - lat) * 512 - 0.5, 0, 4 * SLDEM_H - 1.001), np.mod(lon, 360) * 512 - 0.5, fetch)


def polar(n, root, north):
    """LOLA polar stereographic height (km) of the unit vectors n (MOON_ME) on one hemisphere"""
    p = 1 if north else -1
    t = 2 * R / (1 + p * n[:, 2])
    clip = lambda v: np.clip(v - 0.5, 0, POLAR_N - 1.001)
    path = root / ("ldem_60n_60m.img" if north else "ldem_60s_60m.img")
    print(f"  {path.name}: {len(n)} points", flush=True)
    return bilinear(clip(POLAR_C + p * t * n[:, 0] / POLAR_S), clip(POLAR_C + t * n[:, 1] / POLAR_S),
                    lambda r, c: pick(path, "<i2", POLAR_N, r, c) * 0.0005)


def main(orbit):
    cfg = config()
    df = read_orbit(orbit)
    print(f"{len(df)} rows from {df.utc.iloc[0]} to {df.utc.iloc[-1]}", flush=True)

    for k in sorted(Path(cfg["spice_dir"]).iterdir()):
        sp.furnsh(str(k))
    lat, lon = np.radians(df.lat.to_numpy()), np.radians(df.lon.to_numpy())
    n = np.column_stack([np.cos(lat) * np.cos(lon), np.cos(lat) * np.sin(lon), np.sin(lat)]) @ np.array(sp.pxform("MOON_PA", "MOON_ME", 0.0)).T
    df["lat_me_deg"], df["lon_me_deg"] = np.degrees(np.arcsin(np.clip(n[:, 2], -1, 1))), np.degrees(np.arctan2(n[:, 1], n[:, 0]))

    print("terrain heights", flush=True)
    h = np.empty(len(df))
    low = np.abs(df.lat_me_deg.to_numpy()) <= 60
    h[low] = sldem(df.lat_me_deg.to_numpy()[low], df.lon_me_deg.to_numpy()[low], Path(cfg["sldem_dir"]))
    for north in (True, False):
        m = ~low & ((n[:, 2] > 0) == north)
        if m.any():
            h[m] = polar(n[m], Path(cfg["polar_dir"]), north)
    df["terrain_height_km"], df["real_altitude_km"], df["ideal_altitude_km"] = h, df.alt - h, df.alt

    out = HERE / "outputs"
    out.mkdir(exist_ok=True)
    name = Path(orbit).stem + "_altitudeprofile"
    cols = ["utc", "lat_me_deg", "lon_me_deg", "terrain_height_km", "real_altitude_km", "ideal_altitude_km"]
    df[cols].to_csv(out / f"{name}.csv", index=False, float_format="%.6f")

    plot(df, Path(orbit).name, out / f"{name}.png")
    print(f"real altitude: min {df.real_altitude_km.min():.3f} km at {df.utc[df.real_altitude_km.idxmin()]}\n"
          f"written {out / name}.csv and .png")


def plot(df, title, path):
    """ideal altitude, real altitude (same altitude axis) and their difference, on the same UTC axis"""
    t = pd.to_datetime(df.utc, format="%Y-%m-%dT%H:%M:%S.%f")
    fig, (top, mid, bottom) = plt.subplots(3, 1, figsize=(14, 11), sharex=True)
    mid.sharey(top)
    top.plot(t, df.ideal_altitude_km, lw=0.6, color="C0")
    top.set(ylabel="Ideal altitude (km)", title="Above the 1737.4 km sphere (no relief)")
    mid.plot(t, df.real_altitude_km, lw=0.6, color="C1")
    mid.set(ylabel="Real altitude (km)", title="Above the lunar terrain")
    bottom.plot(t, df.ideal_altitude_km - df.real_altitude_km, lw=0.6, color="C2")
    bottom.set(xlabel="UTC", ylabel="Ideal − real (km)", title="Difference: terrain height under the satellite")
    for ax in (top, mid, bottom):
        ax.grid(alpha=0.3)
    fig.suptitle(title)
    fig.autofmt_xdate()
    fig.tight_layout()
    fig.savefig(path, dpi=150)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
