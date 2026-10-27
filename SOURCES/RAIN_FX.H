#ifndef RAIN_FX_H
#define RAIN_FX_H

#include <SYSTEM/ADELINE_TYPES.H>

#include "RAIN.H" /* T_RAIN, a drop; declares nothing of its own types, so after the types */

struct T_SETTING; /* SETTINGS.H; a pointer to a table needs no more */

/* The storm's look.
 *
 * The 1997 storm draws every drop as an opaque one-pixel line of a grey the
 * fog ramp picks by depth (LineRain), and its splash as an opaque V, and the
 * drops drift with one constant wind, and the box they fall in surrounds the
 * view's target, so the ground in front of the camera sees few of them: the
 * storm's density on screen is its far drops, over the sky. RainFx draws the
 * same drops as translucent streaks instead, blended into what is behind them
 * through the palette mix tables the texture filter uses (PolyBlendLUT),
 * twice as long, and stronger and wider near the camera than far away, lets the
 * wind sway and gust, and starts every fourth drop between the eye and the
 * target, above the view ray, so the ground is rained on too. Off, the
 * default, is byte-identical to the original.
 *
 * Recorded: the wind moves the drops and the streak decides where a drop meets
 * the ground, so the setting reaches the shared random stream like the storm's
 * scale does. */

/* 0: the 1997 storm. 1: translucent, depth-graded streaks and a gusting wind. */
extern S32 RainFx;

/* The arithmetic, over plain values and free of engine state, so the host test
 * in tests/rain_scale covers it without booting anything. */

/* The wind at time t (ms) in 1/256 of the 1997 drift: a slow sway of +/- 90
 * over five seconds and a gust of up to +170 that rises and falls over 1.5 s
 * every 7.3 s. Integer only, so a replay sways the same on every platform. */
static inline S32 RainFx_Wind(S32 t) {
    S32 u = t % 5000;
    S32 g = t % 7300;
    S32 v, sway, gust;
    if (u < 0)
        u += 5000;
    if (g < 0)
        g += 7300;
    v = (u < 2500) ? u : 5000 - u;
    sway = (v - 1250) * 90 / 1250;
    gust = (g < 1500) ? (170 * g / 750) * (1500 - g) / 750 : 0;
    return 256 + sway + gust;
}

/* The gust a lightning strike adds, `age` ms after it, in the same 1/256: up
 * to +200, rising and falling over 1.5 s; nothing before the first strike. */
static inline S32 RainFx_FlashGust(S32 age) {
    if (age < 0 || age >= 1500)
        return 0;
    return (200 * age / 750) * (1500 - age) / 750;
}

/* The storm's intensity at time t (ms), in 1/256 of its count: lulls to two
 * thirds and back over 45 s, and a burst of up to a third more for 1.5 s after
 * a lightning strike `flashAge` ms ago. Integer only, as the wind. */
static inline S32 RainFx_Intensity(S32 t, S32 flashAge) {
    S32 u = t % 45000;
    S32 v, lull, burst;
    if (u < 0)
        u += 45000;
    v = (u < 22500) ? u : 45000 - u; /* 0..22500, a triangle over the cycle */
    lull = 85 * v / 22500;           /* 0 at full strength, 85 at the lull's bottom */
    burst = (flashAge >= 0 && flashAge < 1500) ? (85 * flashAge / 750) * (1500 - flashAge) / 750 : 0;
    return 256 - lull + burst;
}

/* A frame's drift along X for the storm's delta: the 1997 half of it, or that
 * half blown by the wind. */
static inline S32 RainFx_Drift(S32 fx, S32 delta, S32 wind) {
    return fx ? delta * wind / 512 : delta / 2;
}

/* The most a streak at scaled depth z (0 near, 65535 at the far clip) is
 * blended toward its own colour, as the texture filter's table index plus one,
 * with 4 meaning drawn as it is: a near streak has an opaque head, a far one
 * stays at 75%. A 25% mix of the storm's grey into the ground's browns lands
 * on the same palette entry and shows nothing, so no part goes below 50%. */
static inline S32 RainFx_Strength(S32 z) {
    return (z < 12000) ? 4 : 3;
}

/* How strongly pixel i of a streak of `steps` pixels is blended, the tail (0)
 * at 50%, the next quarter at 75%, the head half as it is, capped by the
 * depth's strength above: a drop reads as a bright head with a soft trail. */
static inline S32 RainFx_PixelStrength(S32 i, S32 steps, S32 cap) {
    const S32 part = steps ? (i * 4) / steps : 4;
    const S32 q = (part >= 2) ? 4 : (part == 1 ? 3 : 2);
    return (q > cap) ? cap : q;
}

/* The streak's grey, three steps up its ramp from the fog's pick, so the blend
 * has something to show: the mix pulls it back toward the ground. Stays inside
 * the 16-entry ramp the index is in. */
static inline S32 RainFx_Brighten(S32 c) {
    const S32 top = (c & ~15) + 15;
    return (c + 3 > top) ? top : c + 3;
}

/* The streak's world length, the 1997 delta doubled, and its lean, the X delta
 * blown by the wind (in 1/256 of the 1997 drift), so a gust tilts every streak
 * together. Off, the 1997 delta as it is. */
static inline S32 RainFx_StreakX(S32 fx, S32 dx, S32 wind) {
    return fx ? dx * 2 * wind / 256 : dx;
}

static inline S32 RainFx_StreakYZ(S32 fx, S32 d) {
    return fx ? d * 2 : d;
}

/* One extra column of width for a near streak, none further away. */
static inline S32 RainFx_ExtraWidth(S32 z) {
    return (z < 12000) ? 1 : 0;
}

/* The columns RAIN.CPP draws itself beside each line: none with the look on,
 * whose streak draws its own. */
static inline S32 RainFx_ColumnWidth(S32 fx, S32 width) {
    return fx ? 1 : width;
}

/* The splash box's right edge for an arm ending at x1 at scaled depth z: the
 * 1997 edge, or that plus the columns the streak adds. */
static inline S32 RainFx_SplashRight(S32 fx, S32 x1, S32 streakWidth, S32 z) {
    return fx ? x1 + streakWidth + RainFx_ExtraWidth(z) - 1 : x1;
}

/* The depth ramp of a streak, as LineRain carries it: each depth is shifted
 * into the high half of a 32-bit register, so a depth past the far clip
 * (AffRain scales it past 65535) wraps to a near one, and the difference the
 * ramp steps by is the sign-extended 16-bit one, carried without wrapping
 * again. The start is the wrapped depth; the end, start plus that difference,
 * may run past 65535 or below 0, and the depth test runs in 32 bits there. */
static inline S32 RainFx_DepthStart(S32 z0) {
    return z0 & 0xFFFF;
}

static inline S32 RainFx_DepthDelta(S32 z0, S32 z1) {
    return (S32)(S16)(U16)((z1 - z0) & 0xFFFF);
}

/* The offset of step i of `steps` along an axis of delta d, rounded to the
 * nearest pixel by the magnitude, so a line and its mirror draw the same
 * pixels and both reach their end: C division truncates toward zero, which on
 * a negative delta repeats the first pixel and never draws the last. */
static inline S32 RainFx_Dda(S32 d, S32 i, S32 steps) {
    const S32 a = d < 0 ? -d : d;
    const S32 v = (S32)(((S64)a * i * 2 + steps) / (2 * steps));
    return d < 0 ? -v : v;
}

/* The ripple's half width at age dt (ms), from the splash's half width xp:
 * the V's width, growing to three times it over the splash's life. */
static inline S32 RainFx_RippleRadius(S32 xp, S32 dt) {
    const S32 age = (dt > 80) ? 80 : (dt < 0 ? 0 : dt);
    return xp + 1 + (xp * 2 * age) / 80;
}

/* Whether drop `index` of the pool is one of the near layer, every fourth. */
static inline S32 RainFx_NearDrop(S32 fx, S32 index) {
    return fx && (index & 3) == 0;
}

/* The drops the look runs: a third more than the storm's scale gives, scaled
 * by the intensity (1/256), within the pool, so the near layer, a quarter of
 * the run, adds to the storm; past the pool's cap (a scaled count over 1012,
 * frames above about 1.55 Mpx) it is taken from the far veil instead. Off, the
 * count as it is. */
static inline S32 RainFx_Count(S32 fx, S32 count, S32 intensity, S32 pool) {
    S32 n;
    if (!fx)
        return count;
    n = (count + count / 3) * intensity / 256;
    if (n < 1)
        n = 1;
    return (n > pool) ? pool : n;
}

/* The near layer's half-width at `depth` along the view, from the view's width
 * in 480-line pixels over the 600 focal (viewW / 1200 of the depth), with a
 * margin: the layer fills what the frame sees and little beyond. */
static inline S32 RainFx_NearHalfWidth(S32 depth, S32 viewW) {
    return 800 + (S32)((S64)depth * viewW / 1200);
}

/* The live wind's drift along X for this frame's delta (GereRain). */
extern S32 RainFx_DriftX(S32 delta);

/* The drops to run for the storm's scaled count (RAIN.CPP). */
extern S32 RainFx_DropCount(S32 count);

/* A lightning strike, from the two places that start one (the life script's
 * LM_ECLAIR, the lightning spell): the look gusts and bursts after it. */
extern void RainFx_Lightning(void);

/* Start a near drop: between the eye and the view's target, 15% to 64% of the
 * way, within the view's width there, and above the view ray so it falls
 * across it. Four draws from the shared random stream, one more than
 * InitOneRain takes: how far along the view, two for the lateral offset, one
 * for the height. */
extern void RainFx_InitNear(T_RAIN *pt);

/* The streak's world deltas from the drop (AffRain): the 1997 ones as they
 * are, or doubled and leaning with the live wind. */
extern S32 RainFx_DeltaX(S32 dx);
extern S32 RainFx_DeltaYZ(S32 d);

/* The ripple of a splash: a ring on the ground around the impact (x, y) at
 * scaled depth z, growing with the splash's age dt (ms) from the arms' half
 * width xp, blended at half strength and drawn only on the surface the drop
 * struck (a pixel much nearer or deeper than the impact is something else).
 * Grows the box (bx0, by0)-(bx1, by1) over what it drew. Nothing with the
 * setting off, or for a splash too far away to have a ring wider than its V. */
extern void RainFx_Ripple(S32 x, S32 y, S32 z, S32 xp, S32 dt, S32 c, S32 *bx0, S32 *by0, S32 *bx1, S32 *by1);

/* A drop's streak or a splash's arm, from (x0, y0) at scaled depth z0 to
 * (x1, y1) at z1 in colour c: LineRain's contract (bit 0 of the return, the
 * line showed; bit 1, it met the ground; ScreenXMin..ScreenYMax, what it
 * covered, or LineRain's sentinels when nothing showed), drawn by LineRain
 * with the setting off and as a translucent, depth-graded streak with it on. */
extern S32 RainFx_Line(S32 x0, S32 y0, S32 z0, S32 x1, S32 y1, S32 z1, S32 c);

/* The cfg key, the console cvar and the table, for the surfaces that serve
 * every setting (CONFIG_FILE.CPP, CONSOLE_CMD.CPP), as RAIN_SCALE.H's are. */
extern void RainFx_ReadConfig(void);
extern void RainFx_WriteConfig(void);
extern void RainFx_RegisterCvars(void);
extern const T_SETTING *RainFx_Settings(S32 *count);

#endif /* RAIN_FX_H */
