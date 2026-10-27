# Graphical quality options

Variables and locations that can be changed or exposed as options to improve graphical quality, increase details and effects, etc.

## Shipped options

All five are off by default, so a stock run renders exactly as before. Four of them have a row in the Options → Graphics submenu (see [MENU.md](MENU.md)); interior render scaling is read once at startup and stays environment-only. All of them can also be set from the console or the config file.

| Option | Menu row | Console | lba2.cfg | Environment | Values |
|--------|----------|---------|----------|-------------|--------|
| Texture filtering | Texture filter | `gfx_texfilter` | `TextureFilter` | `LBA2_TEXFILTER` | 0 off, 1 horizontal 2-tap, 2 bilinear 4-tap |
| Dithered shading | Dithered shading | `gfx_dither` | `DitherShading` | (none) | 0 off, 1 on |
| Interior render scaling | (none) | (none) | (none) | `LBA2_ISO_DIV` | 1 off, 2 to 4 |
| Present filter | Scaling | `gfx_presentfilter` | `PresentFilter` | `LBA2_PRESENT_FILTER` | 0 nearest, 1 linear, 2 pixelart |
| Rain scale | Rain | `gfx_rain_scale` | `RainScale` | (none) | 0 the 1997 storm, 1 scaled to the framebuffer |

**Texture filtering** smooths magnified texels on terrain, sea, and sky. The rasterizer works in palette indices, where averaging two entries is meaningless, so the blend is a precomputed table of the nearest palette index to each 25/50/75% RGB mix. Costs roughly 7% of frame time on terrain and 4% on a sea-heavy view at 1728x1080 with the 4-tap setting.

**Dithered shading** applies an ordered dither to Gouraud shade rows, which softens the 16-step banding of the original palette ramps.

**Interior render scaling** renders isometric interiors at 1/N of the chosen resolution and lets the present stretch them back, so the room is drawn larger. Nothing in an interior scales with the framebuffer (bricks are pre-rendered sprites and the iso projectors take no focal argument), so a bigger framebuffer otherwise just reveals more empty space around the room: it covers 29% of the frame at 1728x1080 against 82% at 640x480. Exteriors, whose projection does carry a focal, always render at full resolution.

`LBA2_ISO_DIV` is read once at startup and cannot be changed mid-run. It is also ignored when the divided size would fall below the engine's 320x200 minimum, which is silent: at 640x480 a divisor of 2 works and 3 does nothing.

```
LBA2_ISO_DIV=2 ./lba2cc
```

**Present filter** chooses the filter that stretches the finished frame to the window. Nearest is the original output, and it is uneven whenever the window is not a whole multiple of the resolution: 1728x1080 on a 3840x2400 display is 20/9, so a source pixel is two output pixels wide in one place and three in the next. Linear removes that by blurring everything, the HUD and text included. Pixelart keeps the inside of each pixel sharp and blends only the seam between two of them.

Interior render scaling relies on this same stretch to bring a divided interior back to the window, so the filter also decides how those rooms look.

Pixelart needs SDL 3.4 or newer; a build or a runtime older than that logs a warning and presents with nearest, and the setting is kept for a build that can do it. SDL's software renderer draws pixelart exactly as nearest, so it only shows on a GPU renderer. The console changes the mode on the frame being shown, which makes it the quickest way to compare:

```
gfx_presentfilter 2
```

`LBA2_PRESENT_FILTER` takes the names as well as the numbers, applies to one run and is not written to `lba2.cfg`. A value it does not know is reported in the log and ignored.

```
LBA2_PRESENT_FILTER=pixelart ./lba2cc
```

**Rain scale** sizes the Citadel storm for the frame instead of the 640x480 view the 1997 numbers assume. The original runs 200 drops in a world box sized for that view and draws each as a one-pixel streak (`InitOneRain` and `AffRain` in [SOURCES/RAIN.CPP](../SOURCES/RAIN.CPP)). The exterior focal does not change with the resolution, so a bigger frame is a wider view: it reaches past that box and shows its drops over more pixels, 6.75 times the area at 1920x1080 with the same 200 hairlines in it. With the setting on, the box grows with the view (its sides with the width over 640, its height with the height over 480) so the frame's edges and upper rows see drops, the drop count follows the frame's area over 640x480 (265 at 848x480, 600 at 1280x720, 1350 at 1920x1080), and a streak is as many columns wide as one pixel of a 480-line frame is tall (two from 720 lines). The count is by area and not by the box's volume because the box deepens along every ray as it widens: by a model of the fixed-focal view, counting by area holds the streaks per pixel within 1.2x of the 1997 ones at the 530 camera preset and at 848x480, and within 1.5x at the 300 preset at 1080 lines, where counting by volume put three to five times as many. At 640x480 it changes nothing. The arithmetic is `RainScale_Count`, `RainScale_Span` and `RainScale_Width` in [SOURCES/RAIN_SCALE.H](../SOURCES/RAIN_SCALE.H), covered by `tests/rain_scale`. Not measured on retail data; the frame cost at 1080p is the projection of the extra drops and the streaks drawn twice.

A scaled storm adds one dirty box per frame, the union of its streaks, where the 1997 storm adds one per drop (`RainScale_BoxAdd` in [SOURCES/RAIN_SCALE.CPP](../SOURCES/RAIN_SCALE.CPP)). The list merges a storm's boxes under its pool's pressure anyway, and it walks itself on every add, which a storm of thousands cannot afford.

The setting is recorded: every drop takes three values from the shared random stream when it starts (`InitRain`, or `GereRain` for a drop the count gains) and whenever `AffRain` or `ClearImpactRain` restarts it (`InitOneRain`), so the count is game state, and with the setting on it follows the resolution, which the recording header already compares as `mode.resolution`. The count follows the resolution the player chose (`Res_BaseDimensions`), so an interior drawn at a divided size under `LBA2_ISO_DIV` does not reach it. The exterior field-of-view lock proposed in #734 would make the projection itself scale with the height; the two do not compose yet, and with both on the storm would be denser than either intends.

```
gfx_rain_scale 1
```

## Ideas not yet wired up

- DetailLevel and Shadow are stored in lba2.cfg and exposed via the Options menu; see [CONFIG.md](CONFIG.md) for the full key list.
- `RAIN_RANGE` in `SOURCES/3DEXT/LINERAIN.ASM` (200) is not a draw distance. It is the depth window, in Z-buffer units around a drop's own depth: a pixel nearer than that window hides the streak, one inside it is the ground (bit 2 of `LineRain`'s return, always with bit 1), and `AffRain` turns the drop into its splash there (the `eclat des gouttes`, `Timer` set) and restarts it once the splash has played. The C++ port, `SOURCES/3DEXT/LINERAIN.CPP`, carries it as the literals 100 and 200 and is held to the ASM by `test_linerain`, so a change to it goes through both. A rain draw distance would therefore be a far-Z cull of the drop in `AffRain`, not a change to this constant.
