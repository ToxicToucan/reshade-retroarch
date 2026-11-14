# RetroArch Shaders for ReShade

A ReShade add-on that runs **unmodified RetroArch shader presets** (`.slangp`) on
Direct3D 11, Direct3D 12, OpenGL and Vulkan games (Direct3D 9: experimental), through
[librashader](https://github.com/SnowflakePowered/librashader).

Re-releases of old games usually scale a low-resolution picture up to your screen. A CRT
shader needs the original picture, or scanlines and masks come out at the wrong scale.
The add-on finds the native picture inside the frame (for example 424×240 inside 4K),
runs the preset on it, and puts the result back. Pixels outside the game picture, such
as HD side art, are left alone.

## Install

1. Install [ReShade](https://reshade.me) **with full add-on support** for the game
   (tested with ReShade 6.8.0).
2. Download `RetroArchShaders-<version>.zip` from
   [Releases](https://github.com/Jorl17/reshade-retroarch/releases) and extract it into
   the game's folder, next to its `.exe`.
3. Put shaders in `retroarch-shaders\`, for example the
   [libretro slang-shaders](https://github.com/libretro/slang-shaders). An installed
   RetroArch (default folder, `%APPDATA%\RetroArch` or Steam) is found automatically.
4. **Direct3D 12 games only:** also add Microsoft's DirectX Shader Compiler, which is not
   included. Download `dxc_<date>.zip` from
   [Microsoft's DirectXShaderCompiler releases](https://github.com/microsoft/DirectXShaderCompiler/releases)
   and copy `bin\x64\dxcompiler.dll` and `bin\x64\dxil.dll` next to
   `RetroArchShaders.addon64`. Without them the add-on leaves Direct3D 12 games untouched
   and shows a message. (Not sure which API your game uses? The RetroArch Shaders window
   shows a message if these files are needed.)

The add-on also needs two Microsoft runtimes that most PCs with games already have. If
the window shows that one is missing, install it:
[DirectX End-User Runtime](https://www.microsoft.com/en-us/download/details.aspx?id=35)
and [Visual C++ Redistributable (x64)](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).

## Use

Press **Home** and open the **RetroArch Shaders** window (a tab next to Home, or a
separate window if you have used ReShade in this game before).

- **Shader:** filter, select a preset, press **Use for this ReShade preset**.
- **Shader parameters:** sliders apply live. **Save to this ReShade preset** keeps them,
  **Revert** goes back.
- **Game resolution:** leave on **Detect automatically** unless the detected resolution
  is wrong (see [FAQ](#faq)).

Each ReShade preset gets its own shader, so switching ReShade presets switches shaders.
Set ReShade's **Next/Previous preset key** to do that in game, and **Effect toggle key**
to turn it off.

The choice is stored as `<preset>.slangp` next to the ReShade preset's `.ini`: an
ordinary RetroArch preset referencing the one you picked, plus your changed parameters.

```ini
#reference "../retroarch-shaders/slang-shaders/crt/crt-royale.slangp"
diffusion_weight = "0.010000"
```

You can write these by hand or drop one in while the game runs.

To search more folders, add them to `ReShade.ini`:

```ini
[RetroArchShaders]
ShaderPaths=D:\my-shaders;E:\more-shaders
```

## Resolution modes

| Mode | What it does |
|---|---|
| **Detect automatically** | Finds the native resolution and where the picture sits, and follows resizes, resolution changes and 4:3/16:9 toggles. During menus, fades and title cards, the last good grid stays in use. |
| **Fixed** | Uses the width × height you type for the whole frame. |
| **Whole frame** | Runs the shader on the whole frame as it is. |
| **Downsample** | Averages the frame down to the width × height you choose (default 320×240), then runs the shader on that. For 3D games and smoothly upscaled games, which have no pixel grid to find. See [3D games and PS1-style games](#3d-games-and-ps1-style-games). |

The window shows the grid in use, for example
`Running on 424x240 in 3840x2160 at (0,0), match 100%`, or in Downsample mode
`Running on 427x240 in 3840x2160 at (0,0), averaged`.

## 3D games and PS1-style games

A 3D game drawn at your screen's resolution has no low-resolution picture inside it to
find, and **Fixed** takes one pixel per block, which gives jagged edges and shimmering.
**Downsample** instead averages every block of pixels into one (an area average), like
rendering at the low resolution with heavy anti-aliasing, so the CRT shader gets a clean,
smooth low-resolution picture.

**Game resolution → Downsample**, then:

- **Size:** pick one of the buttons (320x240, 256x224, 320x224, 384x224, 640x480) or type
  a width and height. The size is for a 4:3 picture; the number of lines (the height) is
  what matters most for scanlines.
- **Keep the game's shape** (on by default): a wider picture keeps its shape and gets more
  columns at the same pixel width, so a 16:9 game at 320x240 runs on 427x240. Off: always
  exactly the width × height you chose, squeezed to fit.
- **Crop:** for games with black bars or a border drawn into the picture, leave out a
  percentage of the frame at each edge. **Crop to 4:3 in the middle** sets it for a 4:3
  picture centred in a wider window. Cropped edges are left untouched.

Settings are saved in `ReShade.ini` (`[RetroArchShaders]`: `NativeMode=3`,
`DownsampleWidth`, `DownsampleHeight`, `DownsampleStretch`, `DownsampleCropLeft`, ...),
like the other resolution settings.

Which size to pick:

| Game | Size |
|---|---|
| N64-style 3D (and Ship of Harkinian, below) | 320×240 |
| PS1-style 3D | 320×240 (or 256×224 / 640×480 for games that used them) |
| SNES-style pixel art, smoothly upscaled | 256×224 |
| Mega Drive-style pixel art, smoothly upscaled | 320×224 |
| Arcade (CPS-style) | 384×224 |

Pixel art that is upscaled sharply (each pixel a block of the same colour) works best with
**Detect automatically**, which finds the exact grid; use Downsample when detection never
finds one.

### Example: Ship of Harkinian (Ocarina of Time)

Ship of Harkinian draws its 3D at the full window resolution with Direct3D 11 (default) or
OpenGL. To run a CRT preset on it as on an N64:

1. Install ReShade for `soh.exe` with **full add-on support**, choosing Direct3D 10/11/12
   (or OpenGL if you switched the game to it).
2. Extract `RetroArchShaders-<version>.zip` into the game's folder, next to `soh.exe`.
3. If your shaders are in a RetroArch install elsewhere, add the folder to `ReShade.ini`:
   ```ini
   [RetroArchShaders]
   ShaderPaths=C:\path\to\RetroArch\shaders
   ```
4. Start the game, press **Home**, open **RetroArch Shaders**, pick a CRT preset (for
   example `crt-guest-advanced-ntsc`) and press **Use for this ReShade preset**.
5. **Game resolution → Downsample**, press **320x240**. With the game in widescreen this
   runs on 427x240; for a 4:3 picture with bars at the sides, press **Crop to 4:3 in the
   middle**.

Lower the game's own internal resolution or turn off its anti-aliasing if you like; the
add-on averages whatever the game draws.

## FAQ

### The effect covers only a box, or part of the screen has black lines

The detected picture is only part of the game's picture, usually because a menu or title
card covered the rest. The add-on checks for this, but if it still happens in your game,
set the resolution by hand: **Game resolution → Fixed**, then type the native **Width**
and **Height**.

To find them, look at the `Running on ...` line during normal gameplay: `424x240` means
width 424, height 240. Common values: Mega Drive 320×224, SNES 256×224, NES 256×240,
Sonic Origins widescreen 424×240.

Only use Fixed when the game fills the screen at a single resolution. For games with
bars at the sides, stay on automatic and press **Detect again**.

### Blurry picture, or scanlines at the wrong size

The resolution in use is wrong. Same fix as above.

### Does it work with HDR?

- **HDR screen, SDR game:** yes. This is the usual case: Windows shows the game's normal
  picture on the HDR screen, and the add-on works on that picture.
- **HDR content** (the game's own HDR option turned on): no. RetroArch shaders are
  written for SDR, so the add-on leaves the picture untouched and shows a message. Turn the
  game's HDR option off.

### My game uses Direct3D 12 and nothing happens

The window (and `ReShade.log`) shows `Direct3D 12 needs dxcompiler.dll and dxil.dll`.
librashader needs Microsoft's DirectX Shader Compiler to run shaders on Direct3D 12, and
it is not included with the add-on: Microsoft's licence for `dxil.dll` has conditions on
redistributing it, so you download it from Microsoft yourself. See step 4 of
[Install](#install).

### How do I report a detection problem?

In ReShade's Settings, turn on **Save before and after images**, take a screenshot while
it happens, and include the `Before.png`.

## Troubleshooting

The RetroArch Shaders window and `ReShade.log` show what is happening.

| Message | Fix |
|---|---|
| `librashader.dll not found` | Put `librashader.dll` next to `RetroArchShaders.addon64`. |
| `librashader.dll could not be loaded: install ...` | Install the runtime in the message (links under [Install](#install)). |
| `Paused: ReShade is not rendering effects` | Effects are toggled off, or `reshade-shaders\Shaders\RetroArchShaders.fx` is missing. |
| `Waiting: looking for the game's pixels` | Needs a scene with detail. Use **Fixed** if it never finds one. |
| Red error under the shader name | The preset failed to load. Check it in RetroArch. |
| `HDR output is not supported yet` | Turn the game's HDR option off. |
| `Direct3D 12 needs dxcompiler.dll and dxil.dll` | Add Microsoft's DirectX Shader Compiler: step 4 of [Install](#install). |
| `... is not supported yet` | The game uses a graphics API the add-on does not support yet (see Limitations). |
| `Vulkan: could not find ...` or `Vulkan: the game's Vulkan queue could be in several ...` | The add-on could not find what it needs on the game's GPU (see Limitations). Please report it with `ReShade.log`. |

## Limitations

- No Direct3D 10 (librashader does not support it).
- Direct3D 9 is experimental: some presets do not load, and some look different
  (crt-royale comes out much darker, for example).
- On Direct3D 12, some presets look slightly different (crt-royale's glow, for example).
- On Vulkan, a few recent games may not work correctly. When the add-on cannot work with a
  game, it leaves it untouched and shows why.
- On Vulkan, each preset reload uses a little GPU memory that is only freed when the game
  closes.
- No HDR content (see FAQ).
- Automatic detection needs sharp pixels. For smoothly upscaled games and 3D, use
  **Downsample**.
- On Direct3D 9, Downsample averages exactly only while each small pixel covers at most 16
  frame pixels across and down (320×240 from frames up to about 4800×3600); above that it
  averages 16 × 16 samples per small pixel instead of every pixel.
- Downsample averages the values the game stored (gamma-encoded, as RetroArch passes a
  core's picture to a preset), not linear light.
- Bezel shaders only cover the game area.

## Build and test

Needs Visual Studio 2022 (or Build Tools) with C++, and Python 3 with `numpy` and
`Pillow` for tests.

```
build.bat
python tools/package.py
```

`build.bat` builds into `out\build\`. `tools/package.py` makes the release zip and
fetches the pinned `librashader.dll`.

| Test | Checks |
|---|---|
| `python tests/make_synthetic.py --procedural out/s` then `out/build/detect_test out/s/manifest.txt` | Detection on generated frames. |
| `out/build/detect_test --rules` | Which detections may replace the grid in use. |
| `python tests/host_selftest.py` | The test host (a stand-in game for D3D9/10/11/12, OpenGL and Vulkan) shows its pictures byte for byte on every API and format. |
| `out/build/files_test`, `out/build/params_test` | Preset files, discovery, saving parameters. |
| `python tests/downsample_test.py` | Downsample mode's average against numpy's, offline on Direct3D 11, 12 and 9 (needs a GPU). |
| `python tests/e2e.py ...` | The add-on inside real ReShade, on each graphics API. |
| `python tests/compat_sweep.py ...` | Loads every preset in a shader folder. |

### What CI runs

CI runs all of the above, but the end-to-end test only on OpenGL: CI machines have no
GPU, and ReShade does not run on the software Direct3D and Vulkan drivers there. Before a
release, run the end-to-end test on a PC with a GPU, from a normal prompt (the Vulkan part
does not work as administrator):

```
python tests/e2e.py --reshade <ReShade64.dll> --librashader <librashader.dll> --shaders <slang-shaders> --preset <preset> --native <native.png>
```

If a ReShade Vulkan layer is installed system-wide (ReShade's setup for a Vulkan game puts one in `C:\ProgramData\ReShade`), it loads instead of the one under test and the Vulkan part fails with no screenshots; run the test with `VK_LOADER_LAYERS_DISABLE=~implicit~` set.

`tests/ci_deps.py <folder>` downloads what CI uses (pinned versions).

### Vulkan validation layer

`tests/e2e.py --vulkan-validation <folder>` also checks every Vulkan call with Khronos'
validation layer, which reports incorrect use of Vulkan, even where drivers accept it. The layer
comes with the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home). To get it without
installing anything, run the SDK installer with `copy_only=1`; it then only copies the
files:

```
vulkansdk-windows-X64-1.4.363.0.exe --root C:\VulkanSDK\1.4.363.0 --accept-licenses --default-answer --confirm-command install copy_only=1
```

Then pass `--vulkan-validation C:\VulkanSDK\1.4.363.0\Bin`.

[docs/assumptions.md](docs/assumptions.md) lists what the add-on relies on in ReShade,
librashader and the graphics APIs beyond their documentation, and how each is checked.
Re-run the end-to-end tests on every API after updating ReShade or librashader.

## Later

Ideas not built yet:

- **Leave the game's HUD out of the shader:** keep text and interface drawn at full
  resolution sharp while the CRT shader runs on the 3D scene underneath (needs a way to
  tell them apart, for example the depth buffer, or a mask the user draws).
- **Find black bars in Downsample mode:** use the bar search of automatic detection to set
  the crop by itself, instead of a percentage typed by hand.

## Licence

MIT. `librashader.dll` is MPL-2.0 and shipped unmodified; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Shaders are not included.
