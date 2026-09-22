# Moon Simulation

An OpenGL rendering of the Moon using real NASA data: a LROC colour mosaic for
the surface texture and a LOLA elevation model for displacement and bump mapping.

## Requirements

- [MSYS2](https://www.msys2.org/) with the UCRT64 toolchain
- Python 3 with [Pillow](https://pillow.readthedocs.io/) (for the one-time asset conversion)

Install the compiler and the two libraries from an MSYS2 shell:

```sh
pacman -S mingw-w64-ucrt-x86_64-gcc \
          mingw-w64-ucrt-x86_64-freeglut \
          mingw-w64-ucrt-x86_64-glew
```

## 1. Get the assets

The texture data is several hundred megabytes and is not in this repository.
Download both files from the NASA Scientific Visualization Studio
[CGI Moon Kit](https://svs.gsfc.nasa.gov/4720/) and put them in `assets/`:

| File | What it is |
| --- | --- |
| `assets/ldem_16.tif` | LOLA elevation map, 5760 x 2880, 32-bit float |
| `assets/lroc_color_poles_8k.tif` | LROC colour mosaic, 8192 x 4096 |

Both sizes are mandatory. `main.cpp` hard-codes them, and a smaller elevation
map will read past the end of its buffer.

## 2. Convert the colour map

```sh
python convert_assets.py
```

This writes `assets/lroc_color_poles_8k_flipped.bmp`. The conversion is required:
the BMP loader in `bmpToTexture.cpp` only accepts an uncompressed 24-bit BMP with
a 40-byte header, while the NASA file is an LZW-compressed TIFF. The image is
also flipped vertically so that its orientation matches the elevation map.

The elevation map needs no conversion.

## 3. Build

```sh
build.bat
```

Or directly:

```sh
g++ -DWIN32 -std=gnu++17 -O2 -w -I. \
    main.cpp sphere.cpp bmpToTexture.cpp tifReader.cpp glslprogram.cpp \
    -lfreeglut -lglew32 -lopengl32 -lglu32 \
    -o MoonSimulation.exe
```

`-std=gnu++17` matters. Plain `-std=c++17` hides `M_PI` behind `__STRICT_ANSI__`
and the build fails.

## 4. Run

```sh
build.bat run
```

Run it from the project root, since the asset paths are relative. Startup takes
a few seconds: the sphere is tessellated into display lists at 1440 x 1440.

### Controls

| Input | Action |
| --- | --- |
| Left drag | Rotate |
| Middle drag | Zoom |
| `+` / `-` | Increase / decrease terrain height |
| `D` | Toggle displacement mapping |
| `B` | Toggle bump mapping |
| `L` | Toggle lighting |
| `V` | Toggle fly-by view |
| `F` | Toggle fly-by track |
| `R` | Reset the view |

## Building with Visual Studio instead

`MoonSimulation.sln` also works, with two caveats. The bundled `freeglut.lib` and
`glew32.lib` are 32-bit, and you must supply matching 32-bit `freeglut.dll` and
`glew32.dll` next to the executable. Only the **Debug | Win32** configuration is
usable: `Debug|x64` declares no link libraries, and neither Release configuration
defines `WIN32`, which is what guards the `glewInit()` call.
