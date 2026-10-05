"""QC of a LINNA-2 dataset folder (specs 06) and the estimate of the full dataset from it.
Usage: python qc.py <dataset folder> <linna2 config.ini>
Writes <folder>/QC_REPORT.md with its figures in <folder>/qc/. Reads the index files, a random sample of the frame JSON and PNG
files, the S2 truth tables, qc/angle_check.* and qc/repro.csv (linna2.exe anglecheck and repro), and MANIFEST.json."""
import glob
import json
import math
import os
import random
import re
import shutil
import sys

import matplotlib
import numpy as np
import pandas as pd
from PIL import Image, ImageDraw

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

MU, R = 4902.800118, 1737.4  # DE440 lunar GM (km^3/s^2), reference sphere (km)
root, cfg_path = sys.argv[1], sys.argv[2]
qc = os.path.join(root, "qc")
os.makedirs(qc, exist_ok=True)
rng = random.Random(0)


def ini(path):
    out = {}
    for line in open(path, encoding="utf-8"):
        line = re.split(r"(?:^|\s)#", line)[0]
        if "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip()
    return out


cfg = ini(cfg_path)
nums = lambda key, default: [float(x) for x in cfg.get(key, default).split()]  # noqa: E731
index = pd.concat([pd.read_csv(f, dtype={"orbit": str, "clip_id": str, "window_split": str, "profile": str}).assign(set=os.path.basename(f)[:-4])
                   for f in glob.glob(os.path.join(root, "index", "*.csv")) if not f.endswith("runs.csv")], ignore_index=True)
day = index[index.night_shortcut == 0]
report = []


def md(df):
    """markdown table (pandas' to_markdown needs the tabulate package)"""
    cols = [str(c) for c in df.columns]
    return "\n".join(["| " + " | ".join(cols) + " |", "|" + "---|" * len(cols)] + ["| " + " | ".join(str(v) for v in r) + " |" for r in df.itertuples(index=False)])


def section(n, title, status, text):
    report.append(f"## {n}. {title}: {status}\n\n{text}\n")


def figure(name):
    plt.tight_layout()
    plt.savefig(os.path.join(qc, name), dpi=110)
    plt.close("all")
    return f"![{name}](qc/{name})"


def load16(path):
    return np.array(Image.open(path)).astype(np.uint16)


def rgb8(a16):
    return Image.fromarray((a16 >> 8).astype(np.uint8)).convert("RGB")


def ellipse_points(x, y, a, b, t, n=90):
    s = np.linspace(0, 2 * np.pi, n)
    return list(zip(x + a * np.cos(s) * np.cos(t) - b * np.sin(s) * np.sin(t), y + a * np.cos(s) * np.sin(t) + b * np.sin(s) * np.cos(t)))


def corners_filled(df):
    """frame-filled rule from the index: the four corner rays of the true camera hit the Moon sphere"""
    w, x, y, z = (df[c].to_numpy() for c in ("q_w", "q_x", "q_y", "q_z"))
    Rm = np.stack([np.stack([1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)], -1),
                   np.stack([2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)], -1),
                   np.stack([2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)], -1)], 1)  # camera (OpenCV) -> ME
    c = df[["r_me_x", "r_me_y", "r_me_z"]].to_numpy()
    t = np.tan(np.radians(df.fov_deg.to_numpy()) / 2)
    ok = np.ones(len(df), bool)
    for sx in (-1, 1):
        for sy in (-1, 1):
            d = np.einsum("nij,nj->ni", Rm, np.stack([sx * t, sy * t, np.ones_like(t)], -1))
            d /= np.linalg.norm(d, axis=1)[:, None]
            b = (c * d).sum(1)
            ok &= (b < 0) & (b * b - (c * c).sum(1) + R * R >= 0)
    return ok


# frames whose JSON is read: rendered ones of every set, with tilted and wide views, and some night frames
picked = []
for _, g in day.groupby("set"):
    picked += g.sample(min(len(g), 60), random_state=0).file.tolist()
for sel in (day[day.tilt_deg >= 25], day[day.fov_class == 90]):
    picked += sel.sample(min(len(sel), 40), random_state=1).file.tolist()
frames = {f: json.load(open(os.path.join(root, f + ".json"))) for f in dict.fromkeys(picked)}
craters = pd.DataFrame([dict(c, file=f, regime=j["sun"]["regime"], tilt=j["camera_true"]["tilt_deg"], fov=j["camera_true"]["fov_nominal_deg"],
                             x=c["ellipse_px"][0], y=c["ellipse_px"][1], a=c["ellipse_px"][2], b=c["ellipse_px"][3], th=c["ellipse_px"][4],
                             cx=c["center3d_px"][0], cy=c["center3d_px"][1]) for f, j in frames.items() for c in j["craters"]])
pos = craters[craters.label == "pos"] if len(craters) else craters
version = next(iter(frames.values()))["generator_version"] if frames else "unknown"
report.append(f"# LINNA-2 QC report\n\nDataset `{root}`: {len(index)} frames ({(index.night_shortcut == 1).sum()} night shortcut) in "
              f"{index.set.nunique()} sets, generator `{version}`; {len(frames)} frame JSON files read for the crater checks.\n")

# 1. angle convention
ac = os.path.join(qc, "angle_check.csv")
if os.path.exists(ac):
    t = pd.read_csv(ac)
    section(1, "Angle convention (04 §6)", "visual check",
            f"{len(t)} elliptical craters (D > 20 km and at least 90 % of the rim traced, the most eccentric of each 9° latitude band), nadir, "
            "north up, 60° FOV, Sun 30° high from the west. Red: the catalogue ellipse with its angle counter-clockwise from east (used); "
            "blue: the same angle read clockwise from north. The red major axes must follow the rims.\n\n![angle](qc/angle_check.png)\n\n" + md(t))
else:
    section(1, "Angle convention (04 §6)", "MISSING", "Run `linna2.exe config.ini anglecheck`.")


def montage(rows, name, cols=10, size=96):
    sheet = Image.new("RGB", (cols * size, math.ceil(len(rows) / cols) * size))
    cache = {}
    for i, r in enumerate(rows.itertuples()):
        img = cache.setdefault(r.file, rgb8(load16(os.path.join(root, r.file + ".png"))))
        s = max(24, 2.6 * r.a)  # crop side in image pixels, then scaled to the tile
        box = (r.x - s / 2, r.y - s / 2)
        crop = img.crop((round(box[0]), round(box[1]), round(box[0] + s), round(box[1] + s))).resize((size, size))
        d, k = ImageDraw.Draw(crop), size / round(s)
        d.line([((px - round(box[0])) * k, (py - round(box[1])) * k) for px, py in ellipse_points(r.x, r.y, r.a, r.b, r.th)], fill=(255, 40, 40))
        d.point([((r.cx - round(box[0])) * k, (r.cy - round(box[1])) * k)], fill=(40, 255, 40))
        sheet.paste(crop, (i % cols * size, i // cols * size))
    sheet.save(os.path.join(qc, name))
    return f"![{name}](qc/{name})"


# 2. projection accuracy
if len(pos):
    sel = pd.concat([pos[pos.tilt >= 25].sample(min(50, (pos.tilt >= 25).sum()), random_state=0), pos[pos.fov == 90].sample(min(50, (pos.fov == 90).sum()), random_state=0)])
    sel = pd.concat([sel, pos.drop(sel.index, errors="ignore").sample(min(200 - len(sel), len(pos) - len(sel)), random_state=0)])
    off = np.hypot(pos.x - pos.cx, pos.y - pos.cy)
    plt.figure(figsize=(6, 4))
    plt.scatter(pos.emission_deg, off, s=3)
    plt.xlabel("emission (deg)"), plt.ylabel("ellipse centre - 3-D centre (px)")
    section(2, "Projection accuracy", "visual check",
            f"{len(sel)} positives ({(sel.tilt >= 25).sum()} with tilt ≥ 25°, {(sel.fov == 90).sum()} at 90° FOV) on the 512² images: red the ellipse label, "
            f"green the projected 3-D centre.\n\n{montage(sel, 'projection_check.png')}\n\nOffset vs emission:\n\n{figure('offset_vs_emission.png')}")
else:
    section(2, "Projection accuracy", "NO DATA", "No positive craters in the frames read.")

# 3. lit fraction
if len(craters):
    lit = craters[craters.regime != "night"]
    ign = (lit.ignore_reason == "lit").mean() if len(lit) else float("nan")  # the ignore band 0.1 <= lit_fraction < 0.5
    reasons = lit.ignore_reason.fillna(lit.label).value_counts(normalize=True).round(3).to_dict()
    plt.figure(figsize=(9, 3))
    for i, rg in enumerate(["terminator", "low", "high", "night"]):
        plt.subplot(1, 4, i + 1)
        plt.hist(craters[craters.regime == rg].lit_fraction, bins=20, range=(0, 1))
        plt.title(rg), plt.xlabel("lit_fraction")
    text = f"Share of the lit ignore band (0.1 ≤ lit_fraction < 0.5) among candidates of day frames: {ign:.1%} (pass < 15 %). " \
           f"All classes and ignore reasons there: {reasons}.\n\n{figure('lit_fraction.png')}\n"
    for lab in ("pos", "ign", "bg"):
        c = craters[(craters.label == lab) & (craters.a > 4)]
        if len(c):
            text += f"\n{lab}:\n\n" + montage(c.sample(min(50, len(c)), random_state=0), f"crops_{lab}.png", size=64)
    section(3, "Lit fraction", "PASS" if ign < 0.15 else "FAIL", text)

# 4. dark frames
night = index[index.night_shortcut == 1]
peaks = [int(load16(os.path.join(root, f + ".png")).max()) for f in night.file.sample(min(200, len(night)), random_state=0)]
pos_dark = int(index[index.dark == 1].n_pos.sum())
section(4, "Dark frames", "PASS" if max(peaks, default=0) == 0 and pos_dark == 0 else "FAIL",
        f"Night-shortcut frames read: {len(peaks)}, largest pixel value {max(peaks, default=0)}. Positives on dark frames: {pos_dark}.\n\n"
        + md(pd.crosstab(index.regime, index.dark.map({0: "not dark", 1: "dark"})).reset_index()))

# 5. size band
if len(pos):
    size = 2 * np.sqrt(pos.a * pos.b)
    bad = int(((size < 9.99) | (2 * pos.a > 204.81) | (pos.D_eff_km < 1.5)).sum())  # the JSON rounds the axes to 0.001 px
    plt.figure(figsize=(6, 3))
    plt.hist(size, bins=50)
    plt.xlabel("2 sqrt(ab) of positives (px)")
    section(5, "Size band", "PASS" if bad == 0 else "FAIL", f"{len(pos)} positives, {bad} outside the band.\n\n{figure('size_band.png')}")

# 6. S1 marginals
s1 = index[index.dataset == "S1"]
if len(s1):
    shares = nums("s1_sun_shares", "0.15 0.10 0.45 0.30")
    got = s1.regime.value_counts(normalize=True)
    fov = s1.fov_class.value_counts(normalize=True)
    polar = (s1.lat_ss_deg.abs() > float(cfg.get("s1_polar_lat_deg", 60))).mean()
    devs = [abs(got.get(k, 0) - v) for k, v in zip(["night", "terminator", "low", "high"], shares)] + [abs(v - 1 / len(fov)) for v in fov] + \
        [abs(polar - float(cfg.get("s1_polar_share", 0.2)))]
    fig, ax = plt.subplots(2, 3, figsize=(11, 6))
    ax[0, 0].hist(s1.fov_deg, bins=60), ax[0, 0].set_xlabel("FOV (deg)")
    for c, g in s1.groupby("fov_class"):
        ax[0, 1].hist(g.alt_terrain_km, bins=30, histtype="step", label=f"{c}°")
    ax[0, 1].legend(), ax[0, 1].set_xlabel("altitude above terrain (km)")
    ax[0, 2].hist(s1.tilt_deg, bins=30), ax[0, 2].set_xlabel("tilt (deg)")
    ax[1, 0].hist(s1.sun_el_b_deg, bins=55), ax[1, 0].set_xlabel("Sun elevation at the boresight (deg)")
    ax[1, 1].hist(s1.lat_ss_deg, bins=36), ax[1, 1].set_xlabel("sub-satellite latitude (deg)")
    ax[1, 2].hist(s1.roll_deg, bins=36), ax[1, 2].set_xlabel("roll (deg)")
    status = ("PASS" if max(devs) <= 0.02 else "FAIL") if len(s1) >= 5000 else "small sample (±2 % needs thousands of frames)"
    section(6, "S1 marginals", status, f"{len(s1)} frames. Sun strata {got.round(3).to_dict()} (expected {shares}); FOV classes {fov.round(3).to_dict()}; "
            f"|lat| > 60°: {polar:.3f}. Largest deviation {max(devs):.3f}.\n\n{figure('s1_marginals.png')}")

# 7. validity rules
if len(s1):
    gsd_bad = int((s1.gsd_m < float(cfg.get("s1_gsd_min_m", 30)) * 0.995).sum())
    limb_bad = int((~corners_filled(s1)).sum())
    spot = [abs(j["boresight"]["gsd_center_m"] - float(index.loc[index.file == f, "gsd_m"].iloc[0])) for f, j in frames.items() if j["dataset"] == "S1"]
    section(7, "Validity rules (S1)", "PASS" if gsd_bad == limb_bad == 0 else "FAIL",
            f"Centre GSD below the minimum: {gsd_bad}; frames with the limb in view: {limb_bad} (index of all {len(s1)} frames; "
            f"the GSD of {len(spot)} JSON files matches it within {max(spot, default=0):.4f} m). The GSD rule is applied before rendering on the "
            "sphere through the local LOLA height; the JSON GSD uses the rendered terrain, hence the 0.5 % tolerance.")

# 8. crater counts
key = np.where(index.dataset == "S1", "S1 " + pd.cut(index.alt_sphere_km, [0, 50, 100, 150, 250]).astype(str), index.dataset + " " + index.orbit.fillna(""))
counts = day.assign(group=key[index.night_shortcut == 0]).groupby(["group", "fov_class"]).n_pos.agg(["count", "median", "mean"]).round(1)
section(8, "Crater counts (rendered frames)", "PASS" if (counts["median"] >= 8).all() else "CHECK (median < 8 positives in some configurations)",
        md(counts.reset_index()))

# 9. leakage (05 §5), the same rules as linna2.exe check
end = {}
for f in glob.glob(os.path.join(root, "S2", "truth", "truth_*.csv")):
    t = pd.read_csv(f, usecols=["t_s"])
    end[os.path.basename(f)[6:-4]] = t.t_s.max()
hours = sum(float(cfg.get(k, d)) for k, d in (("s2_test_hours", 72), ("s2_val_hours", 24), ("s2_guard_hours", 1)))
leak = index[(((index.dataset == "S1") & (index.split != "test")) | ((index.dataset == "S2-clip") & (index.partition == "train"))) & (index.parts & 4 > 0)]
leak = pd.concat([leak, index[(index.dataset == "S1") & (index.split == "train") & (index.parts & 2 > 0)]])
clips = index[index.dataset == "S2-clip"]
leak = pd.concat([leak, clips[clips.t_s >= clips.orbit.map(end).fillna(np.inf) - hours * 3600]])
band = pd.cut(index.lat_b_deg.abs(), [0, 60, 80, 90], labels=["|lat| < 60", "60-80", "> 80"], include_lowest=True)
section(9, "Splits and leakage", "PASS" if leak.empty else "FAIL", f"Leaking frames: {len(leak)}.\n\n" + md(pd.crosstab([index.set, band], index.partition).reset_index()))

# 10. S2 truth
rows = []
for f in sorted(glob.glob(os.path.join(root, "S2", "truth", "truth_*.csv"))):
    t = pd.read_csv(f)
    r = np.linalg.norm(t[["r_mci_x", "r_mci_y", "r_mci_z"]].to_numpy(), axis=1)
    v = np.linalg.norm(t[["v_mci_x", "v_mci_y", "v_mci_z"]].to_numpy(), axis=1)
    e = v * v / 2 - MU / r
    dt = np.diff(t.t_s.to_numpy())
    p = t[["r_me_x", "r_me_y", "r_me_z"]].to_numpy()
    vm = t[["v_me_x", "v_me_y", "v_me_z"]].to_numpy()
    jump = np.linalg.norm(p[1:] - p[:-1] - (vm[1:] + vm[:-1]) / 2 * dt[:, None], axis=1)
    rows.append(dict(orbit=os.path.basename(f)[6:-4], rows=len(t), step_s=f"{dt.min():g}-{dt.max():g}", energy_rel_range=(e.max() - e.min()) / abs(e.mean()),
                     v_over_circular=f"{(v / np.sqrt(MU / r)).min():.4f}-{(v / np.sqrt(MU / r)).max():.4f}", max_step_misfit_m=1000 * jump.max()))
if rows:
    t = pd.DataFrame(rows)
    section(10, "S2 truth", "PASS" if (t.energy_rel_range < 1e-6).all() else "CHECK",
            "Two-body energy v²/2 − μ/r in the Moon-centred J2000 frame (μ = DE440 GM). Its range includes the lunar gravity field of the STK force "
            "model (open check 1), so the 10⁻⁶ criterion only holds for a point-mass propagation. Step misfit: |Δr − v̄ Δt| between rows.\n\n" + md(t))

# 11. Sun epochs
sun = sorted(glob.glob(os.path.join(root, "S2", "truth", "sun_*_k*.csv")))
if sun:
    plt.figure(figsize=(9, 4))
    table = []
    for f in sun:
        name = os.path.basename(f)[4:-4]
        b = pd.read_csv(f)
        plt.plot(b.t_s / 86400, b.beta_deg, lw=0.8, label=name)
        a = pd.read_csv(f.replace("sun_", "arcs_"))
        a = a[a.complete == 1]
        table.append(dict(orbit_epoch=name, beta_deg=f"{b.beta_deg.min():.1f} to {b.beta_deg.max():.1f}", dark_arcs=len(a),
                          duration_min=f"{a.duration_min.min():.1f}-{a.duration_min.max():.1f}" if len(a) else "-"))
    plt.xlabel("days from the first row"), plt.ylabel("beta (deg)"), plt.legend(fontsize=6, ncol=3)
    section(11, "Sun epochs", "visual check", f"{figure('beta.png')}\n\n" + md(pd.DataFrame(table)))

# 12. profile B
pb = index[(index.dataset == "S2-window") & (index.profile == "B")]
if len(pb):
    bad = int((~corners_filled(pb)).sum())
    section(12, "Profile B", "PASS" if bad == 0 else "FAIL", f"{len(pb)} frames, {bad} with the limb in view; attitudes per revolution: "
            f"{pb.groupby(['orbit', 'fov_class']).rev.nunique().to_dict()}.")

# 13. reproducibility
rp = os.path.join(qc, "repro.csv")
if os.path.exists(rp):
    t = pd.read_csv(rp)
    diffs = [int(np.abs(load16(os.path.join(root, f) + ".png").astype(int) - load16(os.path.join(qc, "repro", f) + ".png").astype(int)).max())
             for f in index.set_index("frame_id").loc[t.frame_id[t.png_equal == 0], "file"]]
    section(13, "Reproducibility", "PASS" if t.png_equal.all() and t.json_equal.all() else "PASS (≤ 1 DN)" if max(diffs, default=0) <= 1 else "FAIL",
            f"{len(t)} frames rendered again: {int(t.png_equal.sum())} identical PNG, {int(t.json_equal.sum())} identical JSON, largest difference "
            f"{max(diffs, default=0)} DN.")

# 14. manifest
mf = os.path.join(root, "MANIFEST.json")
need = ["generator_version", "config_hash", "data", "spice_kernels", "orbits", "seeds", "counts"]
have = json.load(open(mf)) if os.path.exists(mf) else {}
section(14, "Manifest", "PASS" if all(k in have for k in need) else "FAIL", f"Missing keys: {[k for k in need if k not in have]}")

# estimate of the full dataset from this folder's frames
runs = pd.read_csv(os.path.join(root, "index", "runs.csv")) if os.path.exists(os.path.join(root, "index", "runs.csv")) else pd.DataFrame()
orbits = cfg.get("orbits", "").split()
full = {f"S1-{s}": n for s, n in zip(("train", "val", "test"), nums("s1_frames", "150000 15000 15000"))}
epochs, combos = cfg.get("s2_epochs", "0 1 2 3 4 5").split(), cfg.get("s2_window_combos", "A0 A3 B0").split()
for o in orbits:
    full[f"S2-clips-{o}"] = float(cfg.get("s2_clips_per_epoch", 500)) * len(epochs) * float(cfg.get("s2_clip_frames", 48))
    val, test = float(cfg.get("s2_val_hours", 24)) * 3600, float(cfg.get("s2_test_hours", 72)) * 3600
    full[f"S2-windows-{o}"] = len(combos) * sum(math.ceil(val / s) + math.floor(test / s) + 1 for s in nums(f"orbit_{o}_steps_s", "1"))
bench = os.path.join(root, "bench", "index")  # S1 rendered in the full run's order (linna2.exe s1 --bench n): its throughput
est = []
for name, n in full.items():
    g = index[index.set == name]
    if name.startswith("S1") and os.path.exists(os.path.join(bench, name + ".csv")):
        g = pd.read_csv(os.path.join(bench, name + ".csv"))
        runs_set = pd.read_csv(os.path.join(bench, "runs.csv"))
    else:
        runs_set = runs
    if g.empty:
        continue
    wall = runs_set[runs_set.set == name].seconds.sum() if len(runs_set) else 0
    k = wall / g.seconds.sum() if wall else 1  # writer threads and progress overhead on top of the per-frame time
    ni = g.night_shortcut.mean()
    per = lambda col, night: g[g.night_shortcut == night][col].mean() if (g.night_shortcut == night).any() else 0  # noqa: E731
    seconds = n * k * (ni * per("seconds", 1) + (1 - ni) * per("seconds", 0))
    bytes_ = n * (ni * (per("png_bytes", 1) + per("json_bytes", 1)) + (1 - ni) * (per("png_bytes", 0) + per("json_bytes", 0)))
    est.append(dict(set=name, sample_frames=len(g), night_share=round(ni, 3), s_per_day_frame=round(per("seconds", 0), 3),
                    MB_per_day_frame=round((per("png_bytes", 0) + per("json_bytes", 0)) / 1e6, 3), full_frames=int(n), hours=round(seconds / 3600, 1),
                    GB=round(bytes_ / 1e9, 1)))
if est:
    t = pd.DataFrame(est)
    extra = sum(os.path.getsize(f) for f in glob.glob(os.path.join(root, "S2", "truth", "*")))
    free = shutil.disk_usage(root).free
    report.append("## Estimate of the full dataset\n\nFrom this folder's frames per set: time and size of a day (rendered) and a night (shortcut) frame, "
                  "weighted by the night share seen here and scaled by the measured run overhead. S1 uses the bench run when there is one "
                  "(the first frames of the full splits in their rendering order, where neighbours share terrain as in the full run).\n\n" + md(t) +
                  f"\n\nTotal: **{t.full_frames.sum():,} frames, {t.hours.sum() / 24:.1f} days, {t.GB.sum() / 1000:.2f} TB** "
                  f"(+ S2 truth tables {extra / 1e9:.1f} GB here). Free space on the output drive: {free / 1e12:.2f} TB.\n")
open(os.path.join(root, "QC_REPORT.md"), "w", encoding="utf-8").write("\n".join(report))
print(f"{os.path.join(root, 'QC_REPORT.md')} written")
