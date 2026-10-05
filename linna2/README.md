# LINNA-2 dataset generator

`linna2.exe` generates the LINNA-2 synthetic datasets from the *series B data-generation specs* (`LINNA-2_specs_B_data_generation/linna2/data`). It uses the simulator's C++/OpenGL renderer, not Blender:
- the per-pixel ray caster on SLDEM2015 512 ppd and the LOLA 60 m polar DEM;
- Hapke photometry on the WAC 643 nm mosaic;
- the soft solar-disc shadows;
- the SPICE Sun;
- the STK orbit fit;
- the Robbins (2018) catalogue.

Everything that can change is in `config.ini`. There is no window: it runs from `run.ps1`.

## Running

```powershell
cd linna2
.\run.ps1 -Sample        # the example, in sample_dir (D:\LINNA\datasets\linna2_sample)
.\run.ps1                # the full dataset, in output_dir (D:\LINNA\datasets\linna2)
.\run.ps1 -Stages s1,check,manifest,qc
```

`run.ps1` builds `linna2.exe` with `mingw32-make` and runs the stages in order. It logs to `<folder>\logs`. The Python scripts use the `linna-env` environment (numpy, pandas, pyarrow, matplotlib, Pillow).

Every frame is listed in `index/<set>.csv` once its PNG and JSON are on disk. A stopped run therefore continues where it stopped. Do not mix generator versions or configurations in one split: the JSON records both (`generator_version`, `config_hash`).

The generator takes about 4 GB of video memory. Close the simulator while it runs.

| Stage | What it does | Writes |
| --- | --- | --- |
| `regions` | `regions.py` (the reference implementation from the specs, seed 2026), once, then frozen | `output_dir/splits/regions.json`, `regions_raster.npy` |
| `catalog` | e_rim of every Robbins crater: mean of the rendered DEM at 64 rim points (04 §1), once | `output_dir/catalog/robbins_linna2.csv` |
| `anglecheck` | 04 §6: the most elliptical crater above 20 km with ≥ 90 % of its rim traced (`ARC_IMG`) in each 9° latitude band, nadir at 60° FOV, Sun 30° high, with its projected ellipse (red) and the alternative angle reading (blue) | `qc/angle_check/*.png`, `qc/angle_check.png` |
| `truth` | 03 §2–3: per orbit, the state at every row in MOON_ME and Moon-centred J2000, β and the dark arcs per Sun epoch | `S2/truth/truth_<orbit>`, `sun_<orbit>_k<k>`, `arcs_<orbit>_k<k>` |
| `s1` | 02: random geometry, all three splits | `S1/<split>/`, `plan/S1-<split>.csv` |
| `clips` | 03 §4: 48-frame clips per orbit | `S2/clips/<orbit>/<clip_id>/`, `plan/S2-clips-<orbit>.csv` |
| `windows` | 03 §5–6: validation and test windows, A-k0, A-k3, B-k0, every FOV of the orbit | `S2/windows/<orbit>/<split>/<profile>_k<k>/<fov>/`, `window_*.csv` |
| `check` | 05 §5 leakage assertions on the index files; exit code 1 on a leak (also run after `s1`, `clips` and `windows`) | console |
| `manifest` | data versions, kernels, orbits, seeds, counts | `MANIFEST.json` |
| `repro` | 06 #13: 100 finished frames rendered again and compared | `qc/repro.csv` |
| `bench` | with `-Sample` only: the first 500 frames of each full S1 split in their rendering order, so `qc` estimates the full S1 from its real throughput (`linna2.exe config.ini s1 --bench 500`) | `sample_dir/bench` |
| `parquet` | the CSV tables above, also as parquet | `*.parquet` |
| `qc` | 06 #1–14 and the estimate of the full dataset from this folder | `QC_REPORT.md`, `qc/*.png` |

With `-Sample` the frame stages make:
- S1: the first `s1_sample_frames` of each split;
- S2 clips: `s2_sample_clips` per FOV of each orbit, at the first Sun epoch;
- S2 windows: the first `s2_sample_window_hours` of every test window.

These are exactly the frames the full run makes first, with the same seeds.

## Frames

- **Camera.** Rendered at 1024² and averaged 2 × 2 in linear radiance to 512².
- **Image.** The simulator's auto-exposure is applied (the 98th percentile of the frame at 0.8). The result is written as a 16-bit gray sRGB PNG at 643 nm. `image.exposure_ev` holds that exposure, so the linear I/F of a pixel is `srgb⁻¹(DN / 65535) / 2^ev`. A frame with no light has `exposure_ev: null`.
- **Night shortcut.** When the Sun is below `night_shortcut_deg` over the whole footprint, the frame is not rendered. Its image is black, every crater is `bg` and the boresight range goes to the LOLA terrain level.
- **Lit test.** The renderer's second output is the direct sunlight pass, cos *i* × the visible fraction of the solar disc, at 1024². A rim sample is lit when the 3 × 3 maximum around it is above 1 % of the frame's 99.5th percentile (`lit_method: "pass"`).
- **Labels (04).**
  - A crater is listed when the centre of its projected rim ellipse is in the image. The ellipse is the exact image of the catalogue rim ellipse: Robbins ellipse fit, centred at its ellipse centre, at e_rim.
  - Each entry has the ellipse in the 512 grid (OpenCV pixels, θ from +x toward +y, radians), the projected 3-D centre, the emission, the Sun elevation, the lit and in-frame fractions, and the class with its reason.
  - On a `dark` frame, a crater that would be `pos` is `ign` with the reason `dark` (user decision). The auto-exposure makes the few lit crests of such a frame bright, so the crater is neither a target nor background.
  - `cid` is the 0-based data row of the Robbins file; `catalog/robbins_linna2` maps it to `CRATER_ID`.
- **Camera in the JSON.**
  - `q_cam_to_me` is (w, x, y, z), from the OpenCV camera frame (+x right, +y down, +z boresight) to MOON_ME.
  - `K512` has its principal point at 255.5.
  - S1 azimuths are clockwise from north. S2 azimuths are in LVLH (0 along the velocity, 90° to the right). Roll follows the simulator: a positive roll turns the image clockwise.
  - S2 uses the simulator's pointing convention: with a sideways tilt, the image top follows the tilt direction.
- **Terrain streaming.** Before a frame is rendered, it waits until the SLDEM/polar tiles are loaded everywhere the high-resolution terrain shows. That covers every pixel finer than 0.67 km and the terrain up to 150 km toward the Sun, which can cast shadows into the frame. Outside the streaming windows the global LOLA map is used. The windows are placed by the frame alone (`Streamer::snap`), so a frame renders the same in any order. S1 frames are rendered along a Hilbert curve of their boresight points, so the terrain is read once per region.

## Index columns

`index/<set>.csv` has one line per frame. It has the JSON's main values plus:
- the quaternion and position;
- `parts`, the partitions touched by the 9 × 9 footprint grid (1 train, 2 val, 4 test);
- the crater counts per class;
- `seconds`, the frame's time on the main thread, and `wait_s`, the terrain wait;
- `terrain_timeout`;
- the PNG and JSON sizes.

`index/runs.csv` logs the wall time of every run.

## Differences from the specs (Blender plan)

| Spec | Here |
| --- | --- |
| Blender/Cycles with a fixed Sun strength and view transform | Hapke ray caster with the simulator's per-frame auto-exposure (user decision) |
| 100 km STK orbit | No such file: the Commissioning orbit (132–169 km, mean 150.4 km) is `150km`, with all four FOVs and steps 10/13/29/50 s from the same rule |
| STK positions as written, velocity by central differences | The simulator's local quartic fit (61 s), which removes the ~9 m quantization of the reports, for the images and the truth (user decision) |
| Parquet tables | Written as CSV by `linna2.exe` and converted by `to_parquet.py` (both kept) |
| `lvlh_camera` of `architecture/reference/geometry.py` (not available here) | The simulator's convention (user decision) |
| Catalogue ellipse angle convention unknown (open check 3) | Taken as counter-clockwise from east; see `qc/angle_check.png` |
| `catalog/robbins_linna2` with "conics" (architecture/02 §1, not available here) | a, b, ψ and e_rim per crater; the conic follows from them |
| Altitude above the local surface (S1) | Above the global LOLA 64 ppd surface, which is known before rendering; `alt_terrain_km` uses the same surface |
| Profile B: frame-filled "at the lowest altitude of that revolution" | At the highest altitude. There the limb comes closest to the boresight, as the specs' own tilt limits show (90°: 24–32° at 30 km, 9–12° at 200 km), so only that keeps the limb out over the whole revolution |
| No positives on dark frames (06 #4) | Achieved with the `dark` ignore reason above, since the auto-exposure brightens the lit crests |

The STK files are read as MOON_ME, with altitudes above the 1737.4 km sphere (open check 2, user decision).
