# Moon Simulation

Real-time OpenGL 4.6 visualization of a CubeSat in lunar orbit (SelenITA).

The program opens on a **start screen**. It is pre-filled from `config.ini` and holds:
- the orbit file (with a *Browse…* file dialog), the start time and the frame of its latitude and longitude (MOON_PA or MOON_ME);
- the orbit view FOV;
- the camera FOV, tilt, azimuth and roll;
- the color mode and earthshine.

The start time is checked against the file's range. The buttons work only when every field is valid, and the values are then saved back to `config.ini`. A loading screen shows the progress.

**GENERATE ORBIT REPORT** writes a CSV instead of opening the simulation. It asks where to save it (`<orbit>_report.csv` is suggested), writes one line per orbit file row (a repeated time stamp once), and returns to the start screen, which shows where the report went. The 30-day, 1 s reports (2.6 million rows, ~330 MB) take about 30 s after loading.

| Column | Meaning |
| --- | --- |
| `date`, `time_utc` | Time of the orbit file row, as written in the file |
| `orbit` | Orbit number, as in the telemetry: time since the first row over the orbital period, from 1 |
| `latitude_deg`, `longitude_deg`, `altitude_km` | Satellite in MOON_ME, altitude over the 1737.4 km sphere |
| `above_terrain_km` | Height over the terrain. It uses the global LOLA 64 ppd terrain (the live telemetry uses SLDEM when streamed), so it can differ by tens of meters |
| `speed_km_s` | Satellite speed |
| `sun_azimuth_deg`, `sun_elevation_deg` | Sun below the satellite: azimuth clockwise from north, elevation over the local horizon |
| `illumination_pct` | Visible fraction of the solar disc at the satellite: 0 in eclipse, partial in the penumbra |
| `albedo_643`, `albedo_566`, `albedo_415` | WAC Hapke-normalized I/F below the satellite |
| `camera_tilt_deg`, `camera_azimuth_deg`, `camera_roll_deg` | Camera pointing from the start screen |
| `center_gsd_m` | Ground sample distance at the image center (empty when the boresight misses the Moon) |

**GENERATE IMAGES** appears when the crater catalogue (`crater_db`) is installed: with the full installer, or `assets/lunar_crater_database_robbins_2018.csv` in a build. It asks for the end time, the crater diameter range and the label format, then for a folder. For every orbit file row from the start to the end time it waits for the high-resolution terrain under the camera, renders the camera image with the exposure converged on it, and writes three files named `hh_mm_ss-YYYYMMDD`, like linna-imagegenerator:

| File | Content |
| --- | --- |
| `img/<stem>.png` | The camera image as shown, 8-bit sRGB, at the sensor size |
| `json/<stem>.json` | `camera`: `lat_deg`, `lon_deg` (0–360, MOON_ME), `height_km`, `fov_deg`, `tilt_deg`, `azimuth_deg`, `roll_deg` (as on the start screen), `heading_deg` (flight direction, clockwise from north) and `ev` (auto-exposure). `sun`: `azimuth_deg` (clockwise from north), `azimuth_rel_deg` (from the flight direction) and `elevation_deg`, below the satellite. `earth`: the same three seen from the ground below the satellite (negative elevation below the horizon), plus `distance_km`, `angle_deg` (angular diameter) and `illuminated_fraction` (its lit fraction seen from the Moon) |
| `label/<stem>.txt` | One line per Robbins (2018) crater in view with a diameter in the range, always class 0 |

The labels follow linna-imagegenerator's `build_labels.py`, computed with the simulator's camera:
- **Conic**: `0 xc yc a b theta_deg r_km depth_km rim_elev_km vis_frac shadow_frac offnadir_deg`.
  - The catalogue's rim ellipse is projected exactly, as the conic through the homography of its plane. linna fits an ellipse to 64 projected rim points, which gives the same conic.
  - `xc`, `yc` are divided by the image size and `a`, `b` by its larger side; `theta_deg` runs from +x toward +y.
  - A crater is kept when some of its rim is inside the image, the whole rim is in front of the camera, and its centre is on the visible side.
  - `vis_frac` is the fraction of the rim inside the image, and `shadow_frac` the fraction of those rim points in shadow.
- **Centre + radius**: `0 x_norm y_norm r_norm r_px r_km depth_km rim_elev_km`, for centres inside the image. `r_px = r_km · f / distance along the boresight`, and `r_norm = r_px / max(W, H)`.

Both formats put each centre at its rim height, the median of the terrain on the crater circle. `depth_km` and `rim_elev_km` follow linna's `measure_depth`: the 85th percentile of the terrain in the 0.8–1.2 R annulus, minus the 15th percentile within 0.35 R. All of it is measured on the SLDEM loaded for the image. The heights are above the 1737.4 km sphere, as the simulator draws the terrain, not relative to a patch mid-height as in linna's Blender scene.

An image takes about 0.5 s when the rows are 1 s apart, and about 2 s when each one needs new terrain (rows ~40 s apart), at 1024 × 1024. Cancel stops after the current image.

The simulation then fills the whole monitor the window was on, in one screen:

- **Orbit** (large, left, interactive) – the Moon lit by the real Sun, the CubeSat (OBJ model, or a marker), and
  the orbit track for ±half a revolution. Free view or follow the satellite.
- **Camera** (right, view only) – what the CubeSat camera sees:
  - SLDEM2015 512 ppd terrain;
  - LROC WAC Hapke-normalized color (643/566/415 nm natural color) with Hapke photometry;
  - soft shadows from the solar disc, earthshine and auto-exposure.
- **Telemetry** (below the camera) – UTC, elapsed time, orbit number, satellite position, altitude, velocity,
  Sun azimuth (clockwise from north) and elevation below the satellite, illumination, surface albedo below the
  satellite (WAC Hapke-normalized I/F at 643/566/415 nm), camera pointing, exposure, ground sample distance, frame rate
  and streaming state.
- **Controls** (bottom) – a timeline over the whole orbit file, time jumps, pause, speed, follow and reset the view, back to
  the start screen, quit.

The CEI, ITA, SelenITA and LINNA logos sit in the four corners.

Both skies show the Hipparcos stars, the planets from Mercury to Saturn, the Earth (Blue Marble, rotating, with
its phase and atmosphere) and the Sun. The camera is physical. The orbit view is illustrative: its stars are
scaled to stay visible, and the Sun has a glow.

Both views use the same per-pixel ray caster. The simulation writes nothing to disk except the start-screen values in `config.ini`; the report and the image generation write where the user picks.

## Requirements

- [MSYS2](https://www.msys2.org/) UCRT64:
  ```sh
  pacman -S mingw-w64-ucrt-x86_64-{gcc,make,glfw,glm,glew,libtiff,libpng}
  ```
- **CSPICE**: download [cspice.zip](https://naif.jpl.nasa.gov/pub/naif/toolkit//C/PC_Windows_VisualC_64bit/packages/cspice.zip)
  and extract only `cspice/include` and `cspice/src/cspice` into `third_party/cspice/`.
  The Makefile compiles it with MinGW on the first build.
- **Hapke parameter maps** (Sato et al. 2014), into `assets/hapke/`:
  `WAC_HAPKEPARAMMAP_{643NM,566NM,415NM}.IMG` from the
  [LROC PDS](https://pds.mcp.nasa.gov/data/store/img/lunar_reconnaissance_orbiter/pds4/lroc/lro-l-lroc-5-rdr/LROLRC_2001/DATA/SDP/WAC_HAPKEPARAMMAP/).
- **Crater catalogue** (optional, for GENERATE IMAGES): the Robbins (2018) CSV as `assets/lunar_crater_database_robbins_2018.csv`.
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

SLDEM and the polar DEM are optional. When their folder is missing, the global 474 m LOLA is used instead.

## Hardware and quality

The simulator needs OpenGL 4.6: an NVIDIA, AMD or Intel GPU with a current driver. Without it, a dialog explains why the simulator cannot start.

The start screen has a **Quality** setting (`quality` = `auto`, `high`, `medium` or `low`):

| Level | Global LOLA and albedo | Earth texture | Orbit view | Video memory |
| --- | --- | --- | --- | --- |
| High | full resolution | 8192 × 4096 | full resolution | ~3.6 GB |
| Medium | half resolution | 8192 × 4096 | full resolution | ~1.6 GB |
| Low | half resolution | 4096 × 2048 | half resolution | ~1.4 GB |

The camera always renders at the sensor resolution. `auto` chooses the level from the video memory the driver reports:
- High from 5.5 GB;
- Medium from 2.5 GB;
- Low below that. It is also used for integrated GPUs and when the driver reports no memory (Intel).

The global maps are stored as two texture-array halves, each padded with 256 columns of its neighbour, so no texture side exceeds the 16384 texels AMD and Intel allow. NVIDIA allows 32768. Changing the quality after the first start reloads only those maps.

The development machine (RTX 2070 SUPER, 8 GB) runs High at 60 fps with under 1 GB of RAM.

## Build and run

```sh
build.bat        # the first run also compiles CSPICE (~3 min)
build.bat run    # or: MoonSimulation.exe [config.ini]
```

The executable has no console window. Fatal errors appear as dialogs, for example a missing data file, a shader the driver rejects, or not enough video memory. For a console build, use `mingw32-make SUBSYSTEM=`.

The executable changes to its own folder at start, so relative paths in `config.ini` resolve next to it, from any shortcut.

The first start loads about 40 s of data, most of it the 3.5 GB of float WAC tiles on disk. Going back to the start screen and starting again reloads only what changed:
- the orbit, when its file changed;
- the global maps, when the quality changed.

## Installer

```powershell
powershell -ExecutionPolicy Bypass -File installer\build.ps1            # full installer (~41 GB), needs Inno Setup 6 in D:\LINNA\tools\InnoSetup6
powershell -ExecutionPolicy Bypass -File installer\build.ps1 -Compact   # compact installer (~5 GB), without SLDEM and the polar DEM
```

Both variants install the same application. The compact one goes to `D:\LINNA\installer\output-compact` as `SelenITA-Setup-1.0.0-Compact.exe`, and running the full installer later over it only adds the high-resolution terrain.

`installer/build.ps1` does the following:
1. Builds the release executable.
2. Stages it in `D:\LINNA\installer\stage` with:
   - the MSYS2 DLLs it loads (found with `ldd`) and their licenses;
   - the shaders, fonts and logos;
   - a `config.ini` with relative `data/...` paths and `quality = auto`;
   - a `README.txt` for end users (requirements, install, run), written for the variant being built;
   - a `LICENSE.txt` with the MIT License (Copyright (c) 2026 SelenITA team, ITA).
3. Runs Inno Setup on `installer/selenita.iss`.
4. Copies `README.txt` and `LICENSE.txt` next to the setup `.exe`.

The installer takes only the files the simulator reads, straight from the paths in `config.ini`: the eight WAC `.IMG` tiles per band, the 32 SLDEM float TIFFs, the two polar `.img`, the SPICE kernels, `hipparcos.bdb`, the three Hapke maps, the kit maps and the Earth texture. The orbit files are the configured `orbit_file` plus those listed in `installer/orbits.txt`, all in `data/orbits`. The CubeSat model (`obj_file` and its `.mtl`) goes to `data/model`.

The output is `D:\LINNA\installer\output\SelenITA-Setup-1.0.0.exe` plus 2 GB `.bin` slices, which must stay next to the `.exe`. The installer:
- shows the MIT License, which must be accepted to continue;
- installs per user, with no administrator rights, into a folder of the user's choice;
- offers Full (~43 GB) or Compact (~5 GB, without SLDEM and the polar DEM); Full also installs the crater catalogue (component `craters`, 238 MB), which enables GENERATE IMAGES, and the compact installer never carries it;
- adds a Start menu entry and an optional desktop shortcut, and registers an uninstaller;
- on reinstall, keeps the user's `config.ini`.

The compact `SelenITA-Setup-1.0.0-Compact.exe` also works on its own. On the Ready page it asks the server for the slices `SelenITA-Setup-1.0.0-Compact-N.bin`. The public share is https://cloud.cei.ita.br/s/6ac9HTioaAAMKxS, reached through its WebDAV URL, the `-DownloadUrl` default of `build.ps1`.
- It downloads each slice that is missing next to the `.exe`, or whose size differs from the server's, and moves it there, since Setup reads its slices from its own folder.
- After a successful installation, a hidden PowerShell deletes the downloaded slices once Setup exits, because Setup keeps the last one open until then.
- Without the server, it installs from the slices next to it. Without either, it stops with a message.

The slices belong to the build that made them, so after every compact build, upload its `.bin` files (and the new `.exe`) to the share, replacing the old ones.

## Configuration (`config.ini`)

The start screen edits `orbit_file`, `start_utc`, `orbit_frame`, `orbit_fov`, `fov`, `tilt`, `azimuth`, `roll`, `color`, `earthshine` and `quality`,
and writes them back to the file, keeping the comments. The other keys are set in the file only.
A `#` starts a comment only at the beginning of a line or after a blank, so paths may contain `#`.

| Key | Meaning |
| --- | --- |
| `obj_file`, `obj_scale`, `obj_rotation` | CubeSat model (empty = marker), units → meters, rotation in degrees X Y Z. Camera frame: x = image right, y = image top, −z = boresight. The SelenITA 16U model (`assets/cubsat`, in cm) uses `0.01` and `0 90 0`: the lens (+X face) points along the boresight, the smallest face (+Y) faces the flight direction and the solar panel face (−Y) the opposite |
| `orbit_frame` | Frame of the orbit file latitude and longitude: `PA` (MOON_PA, rotated to MOON_ME) or `ME` (MOON_ME, used as is) |
| `start_utc`, `speed` | Start time (empty = first row) and initial time factor |
| `orbit_fov`, `orbit_gain` | Vertical FOV and fixed gain of the orbit view |
| `orbit_star_mag` | Faintest star magnitude visible in the orbit view (6.5 ≈ naked eye) |
| `orbit_sat_scale` | Initial value of the orbit view's Satellite slider: the satellite is drawn at this many times its real size (`1` = real) |
| `sensor`, `fov` | Camera resolution and horizontal FOV |
| `tilt`, `azimuth`, `roll` | Off-nadir angle; tilt direction relative to the velocity (0 = forward, 90 = right; the image top follows it); roll (positive turns the image clockwise) |
| `color` | `natural` (643/566/415 nm) or `gray` (643 nm), in both views |
| `earthshine` | `1` lights the night side with earthshine, `0` turns it off |
| `kit_gain`, `kit_offset` | Maps the kit's polar caps to I/F per band (fit on 65–70°) |
| `crater_db` | Robbins (2018) crater catalogue CSV; the GENERATE IMAGES button appears only when it exists |
| `images_end_utc`, `crater_diameter`, `label_format` | Image generation: last row time (empty = start + 10 min), diameter range of the labelled craters (km), `conic` or `circle` labels |

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
| `North Pole`, `South Pole` (orbit view) | Mark the pole and the ±60° latitude ring on the visible side of the Moon |
| `Satellite` slider (orbit view) | Magnification, logarithmic, from `Real` (×1) to ×100 000. The size is fixed in space, so it shrinks on screen as the view moves away. Enlarged, the satellite is drawn over the Moon, except when it is behind it |
| `Camera FOV` (orbit view) | Shows the camera's field of view in translucent yellow: the sides of the view pyramid from the satellite to the Moon and the imaged area on the surface (a ray that misses the Moon stops at the horizon distance) |
| `Axes` (orbit view) | Show the MOON_ME axes in the top right corner: 0° longitude (toward the Earth), 90°E, north. Axes pointing away from the viewer are dimmed |

The executable asks hybrid-graphics drivers for the dedicated GPU (`NvOptimusEnablement`, `AmdPowerXpressRequestHighPerformance`).
The GPU in use is shown in the telemetry panel.

## How it works

- **Orbit**
  - Latitude and longitude are read as MOON_PA and rotated to MOON_ME with `pxform`, or as MOON_ME when `orbit_frame = ME`.
  - A local 4th-order least-squares fit (61 s window, widened to at least 4 samples on each side for sparse or irregular reports) removes the 0.001° quantization.
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
- The Earth does not eclipse the Sun, neither in the terrain lighting nor in the satellite illumination (telemetry and orbit report).
- Above ×150 the DEM streaming pauses and the 474 m LOLA is shown.
