# Moon Simulation

Real-time OpenGL 4.6 visualization of a CubeSat in lunar orbit, in two windows:

- **Órbita** – the Moon lit by the real Sun, the CubeSat (OBJ model, or a marker), and
  the orbit track for ±half a revolution. Free view or follow the satellite.
- **Câmera** – what the CubeSat camera sees: SLDEM2015 512 ppd terrain, LROC WAC
  Hapke-normalized color, Hapke photometry, soft shadows from the solar disc,
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
  `WAC_HAPKEPARAMMAP_{321NM,415NM,689NM}.IMG` from the
  [LROC PDS](https://pds.mcp.nasa.gov/data/store/img/lunar_reconnaissance_orbiter/pds4/lroc/lro-l-lroc-5-rdr/LROLRC_2001/DATA/SDP/WAC_HAPKEPARAMMAP/).

Data (paths in `config.ini`):

| Key | Data |
| --- | --- |
| `sldem_dir` | SLDEM2015 512 ppd DEFLATE-tiled GeoTIFFs (`sldem2015_512_*_float.tif`), ±60° |
| `wac_dir` | `WAC_HAPKE_3BAND_E350*.TIF` (689/415/321 nm), ±70° |
| `lola_dem`, `kit_color` | [CGI Moon Kit](https://svs.gsfc.nasa.gov/4720/) `ldem_64.tif` and `lroc_color_poles.tif`, used beyond ±60°/±70° |
| `spice_dir` | Every kernel in the folder is loaded: `naif0012.tls`, `de440s.bsp`, `moon_pa_de440_200625.bpc`, `moon_de440_220930.tf`, `pck00011.tpc` |
| `orbit_file` | STK report with `Time (UTCG)`, `LatitudeMoon`, `LongitudeMoon`, `AltitudeMoon (km)` |

The hardware in use is an RTX 2070 SUPER (8 GB), which runs this with about 3.5 GB of
VRAM and under 1 GB of RAM.

## Build and run

```sh
build.bat        # the first run also compiles CSPICE (~3 min)
build.bat run    # or: MoonSimulation.exe [config.ini]
```

Run from the project root, because shaders and assets are relative paths. Loading takes about 10–20 s,
most of it reading the WAC mosaic from disk.

## Configuration (`config.ini`)

| Key | Meaning |
| --- | --- |
| `obj_file`, `obj_scale`, `obj_rotation` | CubeSat model (empty = marker), units → meters, rotation in degrees X Y Z. Camera frame: x = image right, y = image top, −z = boresight |
| `start_utc`, `speed` | Start time (empty = first row) and initial time factor |
| `orbit_window`, `camera_window` | `x y width height` of each window |
| `orbit_fov`, `orbit_gain` | Vertical FOV and fixed gain of the orbit view |
| `sensor`, `fov` | Camera resolution and horizontal FOV |
| `tilt`, `azimuth`, `roll` | Off-nadir angle; tilt direction relative to the velocity (0 = forward, 90 = right; the image top follows it); roll (positive turns the image clockwise) |
| `color` | `rgb` (689/415/321 nm false color) or `gray` (689 nm) |
| `dn_gain`, `dn_offset` | I/F = gain·DN + offset per band, fitted against the Hapke maps (R² 0.99) |
| `kit_gain`, `kit_offset` | Maps the kit's polar caps into WAC DN space (fit on 65–70°) |

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
  - LOLA covers the rest. Above 75° both LOLA and the albedo are resampled on the GPU once into an azimuthal polar grid, so the poles show no pinwheel.
- **Light**
  - Hapke 2012 photometry with the 1° parameter maps, as the ratio to the normalization geometry (60°, 0°, 60°).
  - Shadows are soft (fraction of the solar disc visible). Earthshine follows the Earth's phase and distance.
  - The camera auto-exposes on the 98th percentile of lit pixels.

## Limitations

- Beyond ±60° (DEM) and ±70° (color) only 474 m/px LOLA and the kit's polar albedo exist, so
  the camera view is smoother there.
- SLDEM2015 has north–south striping in parts of the far side. It shows in the images because
  the renderer reproduces the data.
- No interreflection and no stars.
- Above ×150 the SLDEM streaming pauses and LOLA is shown.
