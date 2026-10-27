#ifndef RAIN_SCALE_H
#define RAIN_SCALE_H

#include "RES_CATALOG.H" /* RES_MAX_W / RES_MAX_H, the largest framebuffer */

#include <SYSTEM/ADELINE_TYPES.H>

struct T_SETTING; /* SETTINGS.H; a pointer to a table needs no more */

/* The storm at any framebuffer size.
 *
 * RAIN.CPP runs RAIN_DROPS drops in a world box sized for a 640x480 view
 * (InitOneRain) and draws each as a one-pixel streak. With the fixed focal
 * (ExteriorFov 0, EXTFOV.H; ChampX / ChampZ in 3DEXT/VAR_EXT.CPP) a bigger
 * frame is a wider view: it reaches past that box and shows its drops over
 * more pixels. At 1920x1080 the Citadel storm is the same 200 hairlines in
 * 6.75 times the area.
 *
 * RainScale sizes the storm for the frame instead. On, the box grows with the
 * view (its sides with the width over 640, its height with the height over
 * 480) so the frame's edges and upper rows see drops, the drop count follows
 * the view's area over 640x480 (the frame's, or under the exterior view lock
 * the 480-line view it defines, below), which holds the streaks per pixel near
 * the 1997 ones, and a streak is as many columns wide as one pixel of a
 * 480-line frame is tall. Off, the default, is byte-identical to the original.
 *
 * Recorded: every drop takes three values from the shared random stream when it
 * starts (InitRain, or GereRain for a drop the count gains) and whenever AffRain
 * or ClearImpactRain restarts it (InitOneRain), so the count is game state, and
 * with the setting on it follows the resolution the recording header already
 * compares as `mode.resolution`. */

#define RAIN_DROPS 200 /* the 1997 count, for a 640x480 frame */
#define RAIN_BASE_W 640
#define RAIN_BASE_H 480

/* The drop count for a w x h frame: RAIN_DROPS times the frame's area over
 * 640x480, rounded to nearest. The box grows by (w/640) squared times h/480, so
 * the world density falls as the box deepens along every ray of a fixed-focal
 * view; counting by the box's volume instead would put several times the 1997
 * streaks on each pixel, where counting by area holds them within 1.2x at the
 * 530 camera preset and at 848x480, and within 1.5x at the 300 preset at 1080
 * lines, by a model of that view (not measured on retail data). A macro so the
 * pool below is a compile-time size. Plain int: RAIN_DROPS * w * h is 4.1e8 at
 * RES_MAX_W x RES_MAX_H, inside S32, and RAIN.CPP holds RAIN_POOL equal to
 * COMMON.H's MAX_RAIN, TabRain's size, so a value that wrapped would refuse to
 * build rather than undersize the pool. */
#define RAIN_SCALE_COUNT(w, h) ((RAIN_DROPS * (w) * (h) + (RAIN_BASE_W * RAIN_BASE_H) / 2) / (RAIN_BASE_W * RAIN_BASE_H))

/* The pool: the count of the largest framebuffer the engine accepts, so no
 * resolution the player can pick is clamped. COMMON.H's MAX_RAIN, which sizes
 * TabRain, must equal it; RAIN.CPP holds the two equal at compile time, because
 * COMMON.H cannot include this header. */
#define RAIN_POOL RAIN_SCALE_COUNT(RES_MAX_W, RES_MAX_H)

/* 0: the 1997 storm. 1: its density and streak width on the current framebuffer. */
extern S32 RainScale;

/* The arithmetic, over plain values and free of engine state, so the host test
 * in tests/rain_scale covers it without booting anything. */

/* How many drops run for a w x h frame: never more than `pool`, the size of
 * TabRain, and never fewer than one. */
static inline S32 RainScale_Count(S32 scale, S32 w, S32 h, S32 pool) {
    S32 n;
    if (!scale)
        return RAIN_DROPS;
    n = RAIN_SCALE_COUNT(w, h);
    if (n < 1)
        n = 1;
    if (n > pool)
        n = pool;
    return n;
}

/* A side of the box, `span` world units for a frame of `base` pixels on that
 * axis, for one of `size` pixels. */
static inline S32 RainScale_Span(S32 scale, S32 span, S32 size, S32 base) {
    if (!scale)
        return span;
    return span * size / base;
}

/* Whether a frame's dirty boxes are unioned (RainScale_BoxBegin): only when the
 * storm being drawn differs from the 1997 one, so a run the setting changes
 * nothing for keeps the list it had. */
static inline S32 RainScale_Coalesce(S32 nbRain, S32 width) {
    return (nbRain != RAIN_DROPS) || (width != 1);
}

/* How many columns wide a streak is on an h-line frame: one per 480 lines,
 * rounded to nearest, never fewer than one. */
static inline S32 RainScale_Width(S32 scale, S32 h) {
    S32 w;
    if (!scale)
        return 1;
    w = (h + RAIN_BASE_H / 2) / RAIN_BASE_H;
    return w < 1 ? 1 : w;
}

/* The same for the live framebuffer: the resolution the player chose, which
 * every exterior renders at, measured as the exterior view lock (EXTFOV.H)
 * defines the view, so the two settings compose. The spans take the 1997
 * literal and give what the box needs on that axis. */
extern S32 RainScale_DropCount(void);
extern S32 RainScale_StreakWidth(void);
extern S32 RainScale_SpanXZ(S32 span);
extern S32 RainScale_SpanY(S32 span);

/* The view's width in 480-line pixels, from the resolution the player chose,
 * for whatever else sizes itself to the storm's frame (the look's near layer). */
extern S32 RainScale_ViewWidth(void);

/* A streak again one column to the right, (width - 1) times, so a drop on a
 * tall framebuffer is as thick as one pixel was on 480 lines. The caller draws
 * the first column itself, keeps its flags, and calls this only when it showed,
 * so nothing is drawn without a box. The box it then adds is the first column's,
 * (by0, bx1, by1) grown here over each column that showed: a column the left
 * edge clips enters the frame rows above the first one, and a column past the
 * right edge draws nothing. */
extern void RainScale_DrawColumns(S32 x0, S32 y0, S32 z0, S32 x1, S32 y1, S32 z1, S32 c, S32 width, S32 *by0,
                                  S32 *bx1, S32 *by1);

/* The storm's dirty boxes, one frame at a time: Begin before the drops, with
 * the count the caller loops over and the width it draws (the storm being
 * drawn, which can differ from the setting on a frame drawn without a
 * simulation step), Add in place of BoxMovingAdd for each, Flush after. The
 * 1997 storm goes straight to the list, a box per drop; a scaled storm is
 * unioned into one box added by Flush, because the list walks itself on every
 * add and a storm of thousands cannot afford that. The rain covers the frame
 * either way. */
extern void RainScale_BoxBegin(S32 nbRain, S32 width);
extern void RainScale_BoxAdd(S32 x0, S32 y0, S32 x1, S32 y1);
extern void RainScale_BoxFlush(void);

/* The cfg key, the console cvar and the table, for the surfaces that serve
 * every setting (CONFIG_FILE.CPP, CONSOLE_CMD.CPP), as RETAILFIX.H's are. */
extern void RainScale_ReadConfig(void);
extern void RainScale_WriteConfig(void);
extern void RainScale_RegisterCvars(void);
extern const T_SETTING *RainScale_Settings(S32 *count);

#endif /* RAIN_SCALE_H */
