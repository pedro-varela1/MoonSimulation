# Moon Simulation

Real-time OpenGL 4.6 visualization of a CubeSat in lunar orbit, in two windows:

- **Órbita** – the Moon lit by the real Sun, the CubeSat (OBJ model, or a marker), and
  the orbit track for ±half a revolution. Free view or follow the satellite.
- **Câmera** – what the CubeSat camera sees: SLDEM2015 512 ppd terrain, LROC WAC
  Hapke-normalized color (643/566/415 nm natural color), Hapke photometry, soft shadows from the solar disc,
  earthshine and auto-exposure.

Both windows use the same per-pixel ray caster. Nothing is written to disk.

## Requirements

- [MSYS2](https://www.msys2.org/) UCRT64:
  ```sh
  pacman -S mingw-w64-ucrt-x86_64-{gcc,make,glfw,glm,glew,libtiff}
  ```
- **CSPICE**: download [cspice.zip](https://naif.jpl.nasa.gov/pub/naif/toolkit//C/PC_Windows_VisualC_64bit/packages/cspice.zip)
  and extract only `cspice/include` and `cspice/src/cspice` into `third_party/cspice/`.
  The Makefile compiles it with MinGW on the first build.
- **Hapke parameter maps** (Sato et al. 2014), into `assets/hapke/`:
  `WAC_HAPKEPARAMMAP_{643NM,566NM,415NM}.IMG` from the
  [LROC PDS](https://pds.mcp.nasa.gov/data/store/img/lunar_reconnaissance_orbiter/pds4/lroc/lro-l-lroc-5-rdr/LROLRC_2001/DATA/SDP/WAC_HAPKEPARAMMAP/).

Data (paths in `config.ini`):

| Key | Data |
| --- | --- |
| `sldem_dir` | SLDEM2015 512 ppd DEFLATE-tiled GeoTIFFs (`sldem2015_512_*_float.tif`), ±60° |
| `polar_dir` | LOLA polar stereographic DEMs `ldem_60n_60m.img` / `ldem_60s_60m.img` (60 m/px, 60°–90°, [PDS LOLA GDR](https://pds-geosciences.wustl.edu/lro/lro-l-lola-3-rdr-v1/lrolol_1xxx/data/lola_gdr/polar/img/)) |
| `wac_643`, `wac_566`, `wac_415` | Float I/F tiles `WAC_HAPKE_<band>NM_E350*.IMG` (LROC PDS `DATA/MDR/WAC_HAPKE`), ±70° |
| `lola_dem`, `kit_color` | [CGI Moon Kit](https://svs.gsfc.nasa.gov/4720/) `ldem_64.tif` and `lroc_color_poles.tif`, global fallback, and the kit's color beyond ±70° |
| `spice_dir` | Every kernel in the folder is loaded: `naif0012.tls`, `de440s.bsp`, `moon_pa_de440_200625.bpc`, `moon_de440_220930.tf`, `pck00011.tpc` |
| `orbit_file` | STK report with `Time (UTCG)`, `LatitudeMoon`, `LongitudeMoon`, `AltitudeMoon (km)` |

The hardware in use is an RTX 2070 SUPER (8 GB), which runs this with about 3.5 GB of
VRAM and under 1 GB of RAM.

## Build and run

```sh
build.bat        # the first run also compiles CSPICE (~3 min)
build.bat run    # or: MoonSimulation.exe [config.ini]
```

Run from the project root, because shaders and assets are relative paths. Loading takes about 40 s,
most of it reading the 3.5 GB of float WAC tiles from disk.

## Configuration (`config.ini`)

| Key | Meaning |
| --- | --- |
| `obj_file`, `obj_scale`, `obj_rotation` | CubeSat model (empty = marker), units → meters, rotation in degrees X Y Z. Camera frame: x = image right, y = image top, −z = boresight |
| `start_utc`, `speed` | Start time (empty = first row) and initial time factor |
| `orbit_window`, `camera_window` | `x y width height` of each window |
| `orbit_fov`, `orbit_gain` | Vertical FOV and fixed gain of the orbit view |
| `sensor`, `fov` | Camera resolution and horizontal FOV |
| `tilt`, `azimuth`, `roll` | Off-nadir angle; tilt direction relative to the velocity (0 = forward, 90 = right; the image top follows it); roll (positive turns the image clockwise) |
| `color` | `natural` (643/566/415 nm) or `gray` (643 nm), in both windows |
| `earthshine` | `1` lights the night side with earthshine, `0` turns it off |
| `kit_gain`, `kit_offset` | Maps the kit's polar caps to I/F per band (fit on 65–70°) |

## Controls

| Input | Action |
| --- | --- |
| `Space` | Pause |
| `↑` / `↓` | Speed ×1, 2, 5 … 1000 |
| `Backspace` | Speed ×1 |
| `→` / `←` | ±60 s (`Shift`: ±1 orbit, `Ctrl`: ±1 day) |
| `Home` | Back to `start_utc` |
| `F` | Toggle following the satellite |
| `R` | Reset the view |
| Left drag / wheel (orbit window) | Rotate / zoom |
| `Esc` | Quit |

## How it works

- **Orbit**
  - Latitude and longitude are read as MOON_PA and rotated to MOON_ME with `pxform`.
  - A local 4th-order least-squares fit (61 s window) removes the 0.001° quantization.
  - Cubic Hermite interpolation gives smooth position and velocity.
- **Sun and Earth**: `spkpos(…, "MOON_ME", "LT", "MOON")` every frame.
- **Terrain**
  - Rays are marched against the DEM, with a bicubic B-spline when magnified.
  - SLDEM is streamed from disk in 0.5° tiles into a toroidal 32°×16° window. Tiles are read in contiguous runs, which suits an HDD, and loaded ahead along the ground track.
  - The 60 m LOLA polar DEM is streamed the same way into a 491 km polar-stereographic window around the next cap crossing, so SLDEM and the polar DEM join at 60° at the same resolution.
  - The 474 m global LOLA fills everything else. Above 75° it and the albedo are resampled on the GPU once into an azimuthal polar grid, so the poles show no pinwheel.
  - WAC texels with implausible band ratios (shadowed walls near ±70° where the Hapke normalization breaks down) are treated as missing.
- **Light**
  - Hapke 2012 photometry with the 1° parameter maps, as the ratio to the normalization geometry (60°, 0°, 60°).
  - Shadows are soft (fraction of the solar disc visible). Earthshine follows the Earth's phase and distance.
  - The camera auto-exposes on the 98th percentile of lit pixels.

## Limitations

- Beyond ±70° there is no multispectral color: the caps use the kit's monochrome LOLA-derived
  albedo mapped to I/F, so color detail is lower there (relief is still 60 m).
- SLDEM2015 has north–south striping in parts of the far side. It shows in the images because
  the renderer reproduces the data.
- No interreflection and no stars.
- Above ×150 the DEM streaming pauses and the 474 m LOLA is shown.
