# Moon Simulation

Real-time OpenGL 4.6 visualization of a CubeSat in lunar orbit (SelenITA).

The program opens on a **start screen**. It is pre-filled from `config.ini` and holds:
- the orbit file (with a *Browse…* file dialog) and the start time;
- the orbit view FOV;
- the camera FOV, tilt, azimuth and roll;
- the color mode and earthshine.

The start time is checked against the file's range. The button starts only when every field is valid, and the values are then saved back to `config.ini`. A loading screen shows the progress.

The simulation then fills the whole monitor the window was on, in one screen:

- **Orbit** (large, left, interactive) – the Moon lit by the real Sun, the CubeSat (OBJ model, or a marker), and
  the orbit track for ±half a revolution. Free view or follow the satellite.
- **Camera** (right, view only) – what the CubeSat camera sees:
  - SLDEM2015 512 ppd terrain;
  - LROC WAC Hapke-normalized color (643/566/415 nm natural color) with Hapke photometry;
  - soft shadows from the solar disc, earthshine and auto-exposure.
- **Telemetry** (below the camera) – UTC, elapsed time, orbit number, satellite position, altitude, velocity,
  illumination, camera pointing, exposure, ground sample distance, frame rate and streaming state.
- **Controls** (bottom) – a timeline over the whole orbit file, time jumps, pause, speed, follow and reset the view, back to
  the start screen, quit.

The CEI, ITA, SelenITA and LINNA logos sit in the four corners.

Both skies show the Hipparcos stars, the planets from Mercury to Saturn, the Earth (Blue Marble, rotating, with
its phase and atmosphere) and the Sun. The camera is physical. The orbit view is illustrative: its stars are
scaled to stay visible, and the Sun has a glow.

Both views use the same per-pixel ray caster. Nothing is written to disk except the start-screen values in `config.ini`.

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
- **Interface**, all compiled into the executable:
  - [Dear ImGui](https://github.com/ocornut/imgui) 1.92.9b (MIT) in `third_party/imgui/`: the core, plus the GLFW and OpenGL 3 backends;
  - [`stb_image.h`](https://github.com/nothings/stb) in `third_party/`.
- **Assets**:
  - fonts in `assets/fonts/`: [Inter](https://github.com/rsms/inter) Regular/SemiBold and [JetBrains Mono](https://github.com/JetBrains/JetBrainsMono) Regular, both SIL OFL;
  - logos in `assets/logos/`: `logo - transp.png` (CEI), `ita-logo.png`, `Logo_new.png` (SelenITA, also the window icon), `linna_logo.png`.

Data (paths in `config.ini`):

| Key | Data |
| --- | --- |
| `sldem_dir` | SLDEM2015 512 ppd DEFLATE-tiled GeoTIFFs (`sldem2015_512_*_float.tif`), ±60° |
| `polar_dir` | LOLA polar stereographic DEMs `ldem_60n_60m.img` / `ldem_60s_60m.img` (60 m/px, 60°–90°, [PDS LOLA GDR](https://pds-geosciences.wustl.edu/lro/lro-l-lola-3-rdr-v1/lrolol_1xxx/data/lola_gdr/polar/img/)) |
| `wac_643`, `wac_566`, `wac_415` | Float I/F tiles `WAC_HAPKE_<band>NM_E350*.IMG` (LROC PDS `DATA/MDR/WAC_HAPKE`), ±70° |
| `lola_dem`, `kit_color` | [CGI Moon Kit](https://svs.gsfc.nasa.gov/4720/) `ldem_64.tif` and `lroc_color_poles.tif`, global fallback, and the kit's color beyond ±70° |
| `spice_dir` | Every kernel in the folder is loaded: `naif0012.tls`, `de440s.bsp`, `moon_pa_de440_200625.bpc`, `moon_de440_220930.tf`, `pck00011.tpc` |
| `star_catalog` | NAIF [`hipparcos.xdb.Z`](https://naif.jpl.nasa.gov/pub/naif/generic_kernels/stars/) (118k stars), decompressed and converted to `hipparcos.bdb` with the SPICE `tobin` utility. Empty = no stars |
| `earth_texture` | NASA Blue Marble with clouds, [`land_ocean_ice_cloud_8192.tif`](https://eoimages.gsfc.nasa.gov/images/imagerecords/57000/57735/land_ocean_ice_cloud_8192.tif) |
| `orbit_file` | STK report with `Time (UTCG)`, `LatitudeMoon`, `LongitudeMoon`, `AltitudeMoon (km)` |

The hardware in use is an RTX 2070 SUPER (8 GB), which runs this with about 3.7 GB of
VRAM and under 1 GB of RAM.

## Build and run

```sh
build.bat        # the first run also compiles CSPICE (~3 min)
build.bat run    # or: MoonSimulation.exe [config.ini]
```

Run from the project root, because shaders and assets are relative paths.

The first start loads about 40 s of data, most of it the 3.5 GB of float WAC tiles on disk. Going back to the start screen and starting again only reloads the orbit, and only when its file changed.

## Configuration (`config.ini`)

The start screen edits `orbit_file`, `start_utc`, `orbit_fov`, `fov`, `tilt`, `azimuth`, `roll`, `color` and `earthshine`,
and writes them back to the file, keeping the comments. The other keys are set in the file only.

| Key | Meaning |
| --- | --- |
| `obj_file`, `obj_scale`, `obj_rotation` | CubeSat model (empty = marker), units → meters, rotation in degrees X Y Z. Camera frame: x = image right, y = image top, −z = boresight |
| `start_utc`, `speed` | Start time (empty = first row) and initial time factor |
| `orbit_fov`, `orbit_gain` | Vertical FOV and fixed gain of the orbit view |
| `orbit_star_mag` | Faintest star magnitude visible in the orbit view (6.5 ≈ naked eye) |
| `sensor`, `fov` | Camera resolution and horizontal FOV |
| `tilt`, `azimuth`, `roll` | Off-nadir angle; tilt direction relative to the velocity (0 = forward, 90 = right; the image top follows it); roll (positive turns the image clockwise) |
| `color` | `natural` (643/566/415 nm) or `gray` (643 nm), in both views |
| `earthshine` | `1` lights the night side with earthshine, `0` turns it off |
| `kit_gain`, `kit_offset` | Maps the kit's polar caps to I/F per band (fit on 65–70°) |

## Controls

All controls are buttons in the bottom bar. There are no keyboard shortcuts.

| Control | Action |
| --- | --- |
| Timeline | Drag to any instant of the orbit file |
| `Start` | Back to `start_utc` |
| `« 1 day`, `« Orbit`, `‹ 60 s` / `60 s ›`, `Orbit »`, `1 day »` | Jump back / forward |
| `‖ Pause` / `▶ Resume` | Pause or resume |
| `▼` / `▲`, `×N` | Speed ×1, 2, 5 … 1000; clicking the speed resets it to ×1 |
| `Follow satellite` | Toggle between the free view and following the satellite |
| `↺ View` | Reset the orbit view |
| `Settings` | Back to the start screen |
| `Quit` | Quit |
| Left drag / wheel on the orbit view | Rotate / zoom (the camera view is not interactive) |

The executable asks hybrid-graphics drivers for the dedicated GPU (`NvOptimusEnablement`, `AmdPowerXpressRequestHighPerformance`).
The GPU in use is shown in the telemetry panel.

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
- **Sky**
  - Stars are read from the SPICE EK catalog, carried to the start date with their proper motions, and rotated each frame with `pxform("J2000", "MOON_ME")`.
  - Planets come from `de440s`, with V magnitudes from Mallama & Hilton (2018), including Saturn's rings.
  - Each point source has its irradiance relative to the Sun, from its V magnitude and a blackbody color. Stars get their temperature from the spectral type; planets from B−V. The irradiance is spread over the pixel solid angle with a Gaussian PSF (σ = 0.6 px), in the same I/F units as the terrain.
  - In the camera the exposure decides what is visible, as with a real camera: stars show on the night side (earthshine) but not next to sunlit ground.
  - In the orbit view a star of magnitude `orbit_star_mag` peaks at ~24/255, and brighter stars keep their true ratios.
  - The Earth is the Blue Marble texture as Lambert reflectance. It is scaled to the 0.306 albedo used for earthshine, oriented with `IAU_EARTH`, and seen through a single-scattering Rayleigh layer, which gives blue oceans and a bluish limb.
  - The Sun is limb darkened (linear law per band). In the camera it has its true radiance; in the orbit view it is shown unsaturated with a glow.

## Limitations

- Beyond ±70° there is no multispectral color: the caps use the kit's monochrome LOLA-derived
  albedo mapped to I/F, so color detail is lower there (relief is still 60 m).
- SLDEM2015 has north–south striping in parts of the far side. It shows in the images because
  the renderer reproduces the data.
- No interreflection.
- With `tilt = 0` and `fov = 120` at 30 km the camera sees no sky: the horizon is ~79° from nadir, and the image corners ~68°.
- The camera is a rectilinear (pinhole) lens: at 120° it stretches objects near the edges (about 2× in the corners), the Sun and craters alike.
- The Earth's clouds are a fixed 2002 composite. At the current sizes the Earth is 16–34 px wide, so its atmosphere shows only as a tint and a ~1 px bluish rim.
- The Earth does not eclipse the Sun in the terrain lighting.
- Above ×150 the DEM streaming pauses and the 474 m LOLA is shown.
