# HDR output

What the add-on does when a game presents in HDR, what librashader and ReShade offer for
it, and what is not done and why. Checked against **librashader 0.12.0** and
**ReShade 6.8.0** (their headers in `third_party/`).

## What is built

When the game's swap chain is HDR, the add-on runs the user's preset in SDR, as RetroArch
does with its own HDR option, and converts on the way in and out:

1. **In** (capture pass, `src/capture.slang`, parameters `rra_input`, `rra_paper_white`):
   each frame pixel is decoded to linear light (scRGB: 1.0 = 80 nits; HDR10: PQ curve,
   BT.2020 primaries, converted to BT.709), divided by paper white, clipped to 0..1 and
   encoded with a 2.2 power curve. This is done per frame pixel, before the capture's
   centre pick or Downsample average.
2. The user's preset runs on that SDR picture, into an 8-bit texture.
3. **Out** (output pass, `src/output.slang`, the add-on's own one-pass preset): decode
   with 2.2, inverse tone map (luminance up to half of SDR white is shown at that fraction
   of paper white; brighter values rise smoothly so SDR white is shown at the peak), colours
   either converted exactly from BT.709 or, with **Expand gamut**, taken as DCI-P3 colours
   (more vivid), then encoded for the swap chain: scRGB (16-bit float, 1.0 = 80 nits) or
   HDR10 (BT.2020, PQ, 10 bits). The result is copied into the frame like the SDR result.

Settings (overlay section **HDR**, saved in `ReShade.ini` `[RetroArchShaders]`):
`HdrPaperWhite` (default 200 nits), `HdrPeak` (default 1000 nits), `HdrExpandGamut`
(default on), as RetroArch's HDR settings. CRT presets lose much of the light to masks and
scanlines; the inverse tone map gives back the headroom an HDR screen has.

Which swap chains count as HDR:

| Back buffer | Colour space reported by ReShade | Treated as |
|---|---|---|
| 16-bit float RGBA | any | scRGB (Windows always shows these as scRGB) |
| 10-bit RGB | HDR10 (PQ) | HDR10 |
| any | HDR10 HLG | not supported: the frame is left untouched, with a message |
| other | SDR | SDR, as before |

Direct3D 9 has no HDR swap chains; the add-on refuses HDR frames there.

Tested: HDR10 inside ReShade on Direct3D 11, Direct3D 12 and Vulkan (`tests/e2e.py`, run 8:
the exact test preset's result is compared with a numpy model of both conversions). The
test host cannot draw 16-bit float back buffers, so **scRGB is not covered by a test**; it
shares all code with HDR10 except the encode and decode lines in the two shaders, and the
16-bit format mapping for OpenGL (`GL_RGBA16F`) and Vulkan
(`VK_FORMAT_R16G16B16A16_SFLOAT`) in `FrameRenderer::chain_image`.

## What librashader offers

- Frame options `color_space` (SDR, HDR10, scRGB, PQ-in-scRGB), `brightness_nits` and
  `expand_gamut`, passed to shaders as the `HDRMode`, paper-white and `ExpandGamut`
  uniforms (`frame_*_opt_t` in `librashader.h`). Only presets written for HDR read them.
- `libra_preset_color_space`: the colour space a preset's last pass declares (from its
  output format), to pick a swap chain for HDR presets.
- The output image can be any render-target format (`R16G16B16A16_FLOAT`,
  `R10G10B10A2`, ...); librashader draws into what it is given. The add-on uses this for its
  output pass.

Not used yet: running HDR-aware presets (for example Sony Megatron's) in their HDR mode,
with `color_space` set and their output written straight into the HDR frame, skipping the
output pass. It needs `libra_preset_color_space` added to the add-on's loader
(`src/librashader_api.*`), a per-preset choice between that and the output pass, and a test
preset that declares an HDR output. Today such presets run in their SDR mode and go through
the output pass like any other. Listed under "Later" in the README.

## SDR games on an HDR screen: not built

ReShade gives add-ons what would be needed to turn an SDR game's swap chain into an HDR
one: the `create_swapchain` event can change the back buffer format, and
`effect_runtime::set_color_space` changes the colour space used for presentation. The
add-on does not do this, because it cannot do it safely on its own:

- **Everything else in the frame would be wrong.** The game keeps drawing SDR values; in a
  scRGB or HDR10 swap chain they are shown as linear or PQ values (much too dark, wrong
  colours). The add-on would have to convert every pixel of every frame, also when no
  shader is attached to the ReShade preset, outside the game picture's rectangle, and in
  menus. And it runs before ReShade's own effects (`reshade_begin_effects`), so every other
  ReShade effect would then get HDR values it was not written for.
- **Games break when the back buffer format changes.** A game that copies a texture into
  its back buffer (`CopyResource`, common for upscaled games) needs the same format family;
  8-bit to 16-bit float or 10-bit copies fail. HDR colour spaces also need a flip-model swap
  chain, which older games do not create.
- **It is the job of a separate tool**, which must also handle ReShade's overlay and
  screenshots. Dedicated "Auto HDR" ReShade add-ons and Windows Auto HDR exist for this.

So for an SDR game (for example Ship of Harkinian, which as far as is known has no HDR
output), the add-on's output stays SDR. What can be done instead:

- Turn on Windows HDR and run the game in borderless or fullscreen; Windows shows the SDR
  picture at the **SDR content brightness** set in Windows' display settings. Raising that
  slider makes the CRT picture brighter (all of it, without highlights above it).
- Windows **Auto HDR** (Direct3D 11 and 12 games) or NVIDIA **RTX HDR** convert the finished
  SDR frame, after ReShade, to HDR. They see the CRT shader's output like any game picture.
  Whether they work together with ReShade in a given game has not been tested here.
- If a game has its own HDR option, turn it on: the add-on then uses the path above.
