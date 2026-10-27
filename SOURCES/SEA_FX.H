#ifndef SEA_FX_H
#define SEA_FX_H

#include <SYSTEM/ADELINE_TYPES.H>

#include <POLYGON/POLY.H> /* Struc_Point, the projected corners a sea quad is filled from */

struct T_SETTING; /* SETTINGS.H; a pointer to a table needs no more */

/* The open sea in motion.
 *
 * The 1997 exterior sea is a flat plane under the terrain (DrawOneSea,
 * SOURCES/3DEXT/DRAWSKY.CPP, at Y_SEA), quads 8192 world units a side
 * textured from the top-left 128x128 quadrant of SkySeaTexture, drawn with
 * the rest of the terrain on a full redraw and never again until the next
 * one: between them the scene redraws only its objects, so with the camera
 * still the water is a still picture, and the only water that moves is the
 * shallows strip some cubes carry, a 4 fps tile swap. SeaFx slides the plane's
 * texture slowly in one direction (the current), and can mix a finer, turned
 * copy of the same texture into it moving the other way (the chop), so bright
 * and dark patches form and dissolve. Off, the default, is byte-identical to the
 * original.
 *
 * How the plane moves between full redraws: each full redraw keeps the quads
 * it filled, already projected and clipped, and a frame that redraws only
 * objects fills them again when the offset has reached its next step, testing
 * the depths the full redraw left and writing none (the terrain, nearer, keeps
 * its pixels; the sea keeps exactly its own), then copies the result to the
 * background the objects are erased from. Background objects baked into that background on
 * the full redraw (OBJ_BACKGROUND, OBJECT.CPP) write no depth, so the pixels
 * they drew over the sea are noted when they are baked and put back after each
 * refill.
 *
 * Not recorded: the offset comes from the game clock, the refill reads the
 * depths and writes none, and nothing here draws from the shared random
 * stream, so the simulation and the rain's ground test see what they see with
 * the setting off. */

/* 0: the 1997 sea. 1: moving. 2: moving, with chop. */
extern S32 SeaMotion;

/* The sea page: SEA_FX_PAGE texels a side, which the quads span once
   (V_MapU/V_MapV 0..127*256+255, 8.8 fixed point) and RepMask 0x7F7F wraps. */
#define SEA_FX_PAGE 128
#define SEA_FX_UV_WRAP (SEA_FX_PAGE * 256)

/* The arithmetic, over plain values and free of engine state, so the host test
   in tests/sea_fx covers it without booting anything. */

/* The current's speed, in 1/256 texel per second along U and V: three texels a
   second across the page and one and a half along it, a diagonal drift. */
#define SEA_FX_DRIFT_U 768
#define SEA_FX_DRIFT_V 384

/* The step the offset moves by: an eighth of a texel, about half a pixel of
   the near sea at the lighthouse at 1080 lines and one to two pixels from a
   low camera over open water. The sea is filled again only when the offset
   reaches the next step, so the current costs a refill about 24 times a
   second rather than every frame, and a still frame between two steps costs
   nothing. */
#define SEA_FX_STEP 32

/* The page's offset at game time t (ms) for a drift of `speed` (1/256 texel
   per second), in 8.8 texture units within one page, [0, SEA_FX_UV_WRAP),
   on a whole step. 64-bit, so the game clock's whole range wraps the same on
   every platform. */
static inline S32 SeaFx_Origin(U32 t, S32 speed) {
    return (S32)(((S64)t * speed / 1000) & (SEA_FX_UV_WRAP - SEA_FX_STEP));
}

/* A corner's texture coordinate with the offset added: the coordinate the
   quad was given, 0..SEA_FX_UV_WRAP-1, plus an offset within one page, so the
   sum fits the point's 16 bits (at most 65503) and never wraps inside a quad,
   which would turn the texture around between two corners. */
static inline U16 SeaFx_Shift(U16 uv, S32 origin) {
    return (U16)((S32)uv + origin);
}

/* The chop's phase at game time t (ms): a step every SEA_FX_CHOP_MS, which
   moves the finer layer one of its own texels (about half a page texel)
   against the current, along one of its axes and then the other, so the two
   layers slide across each other in steps about half the size and twice as
   often as one diagonal step would make. 256 steps take the layer once round
   its page on both axes. */
#define SEA_FX_CHOP_MS 83
static inline S32 SeaFx_ChopPhase(U32 t) {
    return (S32)((t / SEA_FX_CHOP_MS) & 255);
}

/* Where texel (u, v) of the choppy page takes its finer texel from, as an
   offset into the 256-byte rows of the page: the page itself at twice the
   frequency turned by about 27 degrees, (2u + v, 2v - u), shifted back by the
   phase's half steps on each axis in turn, wrapped to the page. Turned,
   because the page has rows of dark patches every half page, and an
   axis-aligned copy at twice the frequency lines them up into stripes that
   run to the horizon; at an angle they break into short slanted ripples. */
static inline S32 SeaFx_FineTexel(S32 u, S32 v, S32 phase) {
    const S32 fu = (2 * u + v - ((phase + 1) >> 1)) & (SEA_FX_PAGE - 1);
    const S32 fv = (2 * v - u - (phase >> 1)) & (SEA_FX_PAGE - 1);
    return fv * 256 + fu;
}

/* One texel of the choppy page: the page's texel a, its colour ramp kept (the
   high nibble, which the fog CLUT shades within), its step moved by how far
   the finer layer's texel b sits from the page's mean step m, and kept inside
   the ramp's 16 steps. A finer texel at the mean leaves a as it is. */
static inline U8 SeaFx_Mix(U8 a, U8 b, S32 m) {
    S32 s = (S32)(a & 15) + (S32)(b & 15) - m;
    if (s < 0)
        s = 0;
    if (s > 15)
        s = 15;
    return (U8)((a & 0xF0) | s);
}

/* The page's mean step, rounded: the level the finer layer moves the page's
   texels around, so the chop darkens as much as it brightens. `page` is the
   page's first row, `pitch` the bytes between rows. */
static inline S32 SeaFx_MeanStep(const U8 *page, S32 pitch) {
    S32 sum = 0, u, v;
    for (v = 0; v < SEA_FX_PAGE; v++)
        for (u = 0; u < SEA_FX_PAGE; u++)
            sum += page[v * pitch + u] & 15;
    return (sum + SEA_FX_PAGE * SEA_FX_PAGE / 2) / (SEA_FX_PAGE * SEA_FX_PAGE);
}

/* A background object baked into the background: in the box (x0, y0)-(x1, y1),
   note in `keep` every pixel where the frame `log` differs from the
   background `screen` it is about to be copied over: what the bake changes,
   which is what the object drew except where it drew the very colour already
   under it. Such a pixel is not noted and moves with the sea under it; it is
   the sea's colour on the frame that baked it, a colour the sea's ramp holds.
   Buffers of `pitch` bytes a row; the box inclusive and inside them. */
static inline void SeaFx_NoteDrawn(const U8 *log, const U8 *screen, U8 *keep, S32 pitch, S32 x0, S32 y0, S32 x1,
                                   S32 y1) {
    S32 x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            if (log[y * pitch + x] != screen[y * pitch + x])
                keep[y * pitch + x] = 1;
}

/* After a refill: in the box, every pixel noted in `keep` takes the
   background's colour again, the object the refill painted the sea over. */
static inline void SeaFx_PutBack(U8 *log, const U8 *screen, const U8 *keep, S32 pitch, S32 x0, S32 y0, S32 x1,
                                 S32 y1) {
    S32 x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            if (keep[y * pitch + x])
                log[y * pitch + x] = screen[y * pitch + x];
}

/* The live calls, from the exterior draw (SOURCES/3DEXT, OBJECT.CPP). */

/* A full redraw starts (AffGrilleExt): forget the quads and the baked pixels
   of the last one, and set this frame's offset and page. */
extern void SeaFx_FrameBegin(void);

/* The full redraw is drawn (AffGrilleExt, beside FiltreAnimatedPolys, still
   in the Z-buffered bank): forget the quads the terrain hides whole, so a
   refill fills and copies only sea there is to see, and none when the sea is
   all behind the land. */
extern void SeaFx_DropHidden(void);

/* The texture the sea is drawn from: SkySeaTexture, or the choppy page. */
extern U8 *SeaFx_Map(U8 *skySea);

/* A sea quad about to be filled (Draw_Poly, DRAWSKY.CPP): keep its corners for
   the refills, then add this frame's offset to their texture coordinates. */
extern void SeaFx_Quad(Struc_Point *pts, S32 n);

/* A background object is being baked into the background over the clip box
   (x0, y0)-(x1, y1) (OBJECT.CPP): note the pixels it drew, which the refill
   must leave as they are. */
extern void SeaFx_Baked(S32 x0, S32 y0, S32 x1, S32 y1);

/* A frame that redraws only objects (AffScene): when the offset or the chop
   has moved since the sea was last filled, fill the kept quads again with
   them, put the baked pixels back, and copy what changed to the background.
   Nothing with the setting off, no quads kept, or nothing moved. */
extern void SeaFx_Refill(void);

/* The cfg key, the console cvar and the table, for the surfaces that serve
   every setting (CONFIG_FILE.CPP, CONSOLE_CMD.CPP). */
extern void SeaFx_ReadConfig(void);
extern void SeaFx_WriteConfig(void);
extern void SeaFx_RegisterCvars(void);
extern const T_SETTING *SeaFx_Settings(S32 *count);

#endif /* SEA_FX_H */
