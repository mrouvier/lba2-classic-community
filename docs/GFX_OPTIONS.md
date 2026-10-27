# Graphical quality options

Variables and locations that can be changed or exposed as options to improve graphical quality, increase details and effects, etc.

## Shipped options

All eight are off by default, so a stock run renders exactly as before. Seven of them are reachable from the Options → Graphics submenu (see [MENU.md](MENU.md)) and can also be set from the console or the config file; interior render scaling is read once at startup and stays environment-only.

| Option | Menu row | Console | lba2.cfg | Environment | Values |
|--------|----------|---------|----------|-------------|--------|
| Texture filtering | Texture filter | `gfx_texfilter` | `TextureFilter` | `LBA2_TEXFILTER` | 0 off, 1 horizontal 2-tap, 2 bilinear 4-tap |
| Dithered shading | Dithering | `gfx_dither` | `DitherShading` | (none) | 0 off, 1 on |
| Dithered fog | Dithering | `gfx_fogdither` | `FogDither` | (none) | 0 off, 1 on |
| Interior render scaling | (none) | (none) | (none) | `LBA2_ISO_DIV` | 1 off, 2 to 4 |
| Present filter | Scaling | `gfx_presentfilter` | `PresentFilter` | `LBA2_PRESENT_FILTER` | 0 nearest, 1 linear, 2 pixelart |
| Rain scale | Rain | `gfx_rain_scale` | `RainScale` | (none) | 0 the 1997 storm, 1 scaled to the framebuffer |
| Rain look | Rain | `gfx_rain_fx` | `RainFx` | (none) | 0 the 1997 look, 1 translucent streaks, wind and a near layer |
| Exterior view | Exterior view | `gfx_fov` | `ExteriorFov` | (none) | 0 the fixed focal, 1 keep the 1997 vertical view |

**Texture filtering** smooths magnified texels on terrain, sea, and sky. The rasterizer works in palette indices, where averaging two entries is meaningless, so the blend is a precomputed table of the nearest palette index to each 25/50/75% RGB mix. Costs roughly 7% of frame time on terrain and 4% on a sea-heavy view at 1728x1080 with the 4-tap setting. The animated water a cube carries (the shallows around the lighthouse islet, say) is drawn with the rest of the terrain and then redrawn a quarter at a time through the fillers that test the depth without writing it, which objects in water, on the buggy and in the end credits share; those filter while the water is being redrawn and not otherwise (`PolyTexFilterNZW`, [POLYFILT.H](../LIB386/H/POLYGON/POLYFILT.H)), so the water keeps its filtering after the frame that drew it and the objects drawn through them stay unfiltered.

**Dithered shading** applies an ordered dither to Gouraud shade rows, which softens the 16-step banding of the original palette ramps.

**Dithered fog** does the same for the fog on the sea and the sky. Those two take their fog band from the depth as they draw, once a 32-pixel span, sixteen bands between `StartZFog` and the far clip, and a flat sea seen from above crosses the bands' edges as hard horizontal lines a few shades apart. The filler that draws them (`Filler_TextureZFogSmoothZBuf`, [POLYTZF.CPP](../LIB386/pol_work/POLYTZF.CPP); its two siblings for the same polygon type dither the same way) keeps the four bits under the band, how far into it the span is, and picks each pixel's band from them through the 4x4 ordered threshold of the shading dither (`PolyTZF_FogDitherRow`, [POLYTZF.H](../LIB386/H/POLYGON/POLYTZF.H)). The dither is centred on the band: a span in the middle of its band draws the original's band, one on an edge is half each band, so each line becomes a gradient and the sea keeps its tone. The depth is the same along a row for the sea and the sky whenever the camera has no roll, which is the game's camera except where a camera zone sets one (`GammaCam`); there the per-span band shows as 32-pixel steps in the gradient. The land's fog is picked once a polygon and is not dithered. Pixels only: the setting writes no depth and draws nothing from the shared random stream, so recordings do not carry it. The Options → Graphics "Dithering" row cycles Off, Shading and Shading and fog over the two dithers; it reads Fog when only the console has set the fog half, and its next press turns both off.

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

**Rain scale** sizes the Citadel storm for the frame instead of the 640x480 view the 1997 numbers assume. The original runs 200 drops in a world box sized for that view and draws each as a one-pixel streak (`InitOneRain` and `AffRain` in [SOURCES/RAIN.CPP](../SOURCES/RAIN.CPP)). The exterior focal does not change with the resolution, so a bigger frame is a wider view: it reaches past that box and shows its drops over more pixels, 6.75 times the area at 1920x1080 with the same 200 hairlines in it. With the setting on, the box grows with the view (its sides with the width over 640, its height with the height over 480) so the frame's edges and upper rows see drops, the drop count follows the view's area over 640x480 (the frame's, or the 480-line view the exterior view lock below defines: 265 at 848x480, 600 at 1280x720, 1350 at 1920x1080 on the fixed focal), and a streak is as many columns wide as one pixel of a 480-line frame is tall (two from 720 lines). The count is by area and not by the box's volume because the box deepens along every ray as it widens: by a model of the fixed-focal view, counting by area holds the streaks per pixel within 1.2x of the 1997 ones at the 530 camera preset and at 848x480, and within 1.5x at the 300 preset at 1080 lines, where counting by volume put three to five times as many. At 640x480 it changes nothing. The arithmetic is `RainScale_Count`, `RainScale_Span` and `RainScale_Width` in [SOURCES/RAIN_SCALE.H](../SOURCES/RAIN_SCALE.H), covered by `tests/rain_scale`. Not measured on retail data; the frame cost at 1080p is the projection of the extra drops and the streaks drawn twice.

A scaled storm adds one dirty box per frame, the union of its streaks, where the 1997 storm adds one per drop (`RainScale_BoxAdd` in [SOURCES/RAIN_SCALE.CPP](../SOURCES/RAIN_SCALE.CPP)). The list merges a storm's boxes under its pool's pressure anyway, and it walks itself on every add, which a storm of thousands cannot afford.

The setting is recorded: every drop takes three values from the shared random stream when it starts (`InitRain`, or `GereRain` for a drop the count gains) and whenever `AffRain` or `ClearImpactRain` restarts it (`InitOneRain`), so the count is game state, and with the setting on it follows the resolution, which the recording header already compares as `mode.resolution`. The count follows the resolution the player chose (`Res_BaseDimensions`), so an interior drawn at a divided size under `LBA2_ISO_DIV` does not reach it. The two settings compose: under the exterior view lock below the storm measures the frame as that view, 480 lines tall and as wide as the aspect makes it, so at 1920x1080 with both on the box grows by a third on each side and the count to 267, while the streak width still follows the framebuffer's height, since a streak is one of its pixels wide.

**Exterior view** keeps the original's vertical field of view at any resolution. The exterior projection has one focal, `ChampX` / `ChampZ` = 600 ([SOURCES/3DEXT/VAR_EXT.CPP](../SOURCES/3DEXT/VAR_EXT.CPP)), at every resolution, so a pixel covers the same piece of the world at 1080 lines as at 480 and a taller frame shows more world rather than more detail: at 1920x1080 the view is about 116 by 84 degrees against the original's 56 by 44, with the extra height spent on sky and distant cube edges, and the hero a smaller part of the frame. With the setting on, the focal follows the height, `600 x h / 480` (`ExtFov_Focal` in [SOURCES/EXTFOV.H](../SOURCES/EXTFOV.H), set by `Init3DExtView` in [SOURCES/EXTFUNC.CPP](../SOURCES/EXTFUNC.CPP)): the frame shows the 1997 height of world, only the width grows with the aspect ratio (about 71 degrees at 16:9), and detail grows with the resolution, 2.25 times at 1080 lines. At 480 lines it changes nothing. Three things that compensated the taller view stand down with it: the near clip, which moves in with the height so the ground a taller frustum reaches is not culled, is back at its original value (`ExtFov_NearClip`); the Auto camera's HD recompose (`cam_hd`, [CAMERA_INTERNALS.md](CAMERA_INTERNALS.md)) reads the view's height rather than the frame's and does nothing; and the classic camera's off-screen margins, in pixels, follow the focal so the snap fires at the original distance from the edge (`ExtFov_Margin`). Covered by `tests/extfov`.

The setting is recorded: the original reads what is on screen (the lightning spell strikes the enemies drawn, a life script can test a drawn actor, the classic camera snaps on the hero's projected position), so the view decides game state at any height but 480. Sprites follow the focal like the geometry around them (`CalculeScaleFactorSprite` in [SOURCES/EXTFUNC.CPP](../SOURCES/EXTFUNC.CPP)); particle dots, text and the HUD, drawn in pixels, stay as they are. A change from the console or the menu sets the projection again in an exterior and redraws the frame. Not measured on retail data on the authoring machine; the four framing levers [WIDESCREEN.md](WIDESCREEN.md) weighs are unchanged by it, this is the fifth.

```
gfx_fov 1
```

```
gfx_rain_scale 1
```

**Rain look** makes the storm read as one. The 1997 storm draws every drop as an opaque one-pixel line of a grey the fog ramp picks by depth, its splash as an opaque V, drifts the drops with one constant wind, and keeps them in a box around the view's target, so the ground in front of the camera sees few of them: the density on screen is the far drops, over the sky. With the setting on ([SOURCES/RAIN_FX.H](../SOURCES/RAIN_FX.H)), each drop is a streak twice the 1997 length with a solid head and a tail blended into what is behind it through the palette mix tables the texture filter uses (`PolyBlendLUT`, built on demand by `Poly_RequireBlendLUT` whether or not the filter is on), three steps brighter on its 16-step ramp, a column wider and solid near the camera and 75% at most far away; the wind sways by a third over five seconds and gusts to two thirds more every 7.3 seconds, and every streak leans with it; every fourth drop starts between the eye and the view's target, above the view ray and within the view's width there, so the ground is rained on; the look runs a third more drops than the scale gives, within the pool, and the storm lives: it lulls to two thirds and back over 45 seconds, and a lightning strike (`RainFx_Lightning`, from the life script's `LM_ECLAIR` and the lightning spell) adds a gust of up to three quarters more wind and a burst of a third more drops for 1.5 seconds (what the pool leaves: nothing at a frame whose scaled count fills it); each splash leaves a ripple, a ring on the struck surface that triples over the splash's 80 ms, blended at three quarters for its first 40 ms, then at half (`RainFx_Ripple`). A load clears the look's memory of the last strike, so both ends of a recording start from none. The streak keeps `LineRain`'s depth test, its 16-bit wrap included: a depth past the far clip wraps to a near one and the drop is drawn over whatever is there, which is how the 1997 storm's far drops are on screen at all. The Options → Graphics "Rain" row cycles the three looks, Original, Scaled to resolution, Enhanced, over `RainScale` and `RainFx`.

The setting is recorded: the wind moves the drops, the streak decides where a drop meets the ground and the near layer starts drops elsewhere, all of which reach the shared random stream; its wind is integer arithmetic on the game clock, so every platform sways the same. The look's streak is C++ of its own; `LINERAIN.CPP`, the ASM-equivalence-tested line, draws the 1997 look and is untouched.

```
gfx_rain_scale 1; gfx_rain_fx 1
```

## Ideas not yet wired up

- DetailLevel and Shadow are stored in lba2.cfg and exposed via the Options menu; see [CONFIG.md](CONFIG.md) for the full key list.
- `RAIN_RANGE` in `SOURCES/3DEXT/LINERAIN.ASM` (200) is not a draw distance. It is the depth window, in Z-buffer units around a drop's own depth: a pixel nearer than that window hides the streak, one inside it is the ground (bit 2 of `LineRain`'s return, always with bit 1), and `AffRain` turns the drop into its splash there (the `eclat des gouttes`, `Timer` set) and restarts it once the splash has played. The C++ port, `SOURCES/3DEXT/LINERAIN.CPP`, carries it as the literals 100 and 200 and is held to the ASM by `test_linerain`, so a change to it goes through both. A rain draw distance would therefore be a far-Z cull of the drop in `AffRain`, not a change to this constant.
