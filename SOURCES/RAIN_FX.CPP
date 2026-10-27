#include "RAIN_FX.H"
#include "C_EXTERN.H"   /* VueOffsetX/Y/Z, the view's target; MyRnd */
#include "RAIN_SCALE.H" /* RainScale_StreakWidth, RainScale_ViewWidth, RAIN_POOL */
#include "SETTINGS.H"
#include "3DEXT/EXTERN.H" /* LineRain */

#include <3D/CAMERA.H> /* CameraX/Y/Z, the eye the last frame was drawn from */

#include <POLYGON/POLY.H>     /* PtrZBuffer, PTR_TabOffLine, Fill_Flag_Fog, Fill_Logical_Palette */
#include <POLYGON/POLYFILT.H> /* PolyBlendLUT, Poly_RequireBlendLUT */
#include <SVGA/CLIP.H>        /* ClipXMin.. ClipYMax */
#include <SVGA/SCREEN.H>      /* Log */
#include <SVGA/SCREENXY.H>    /* ScreenXMin.. ScreenYMax */
#include <SYSTEM/TIMER.H>     /* TimerRefHR, the wind's clock */

/* See RAIN_FX.H. */

S32 RainFx = 0;

/* The streaks blend through the texture filter's tables: built when the look
   is switched on rather than in the first rainy frame, where it is a hitch.
   RainFx_Line still asks for them, for a palette set after the switch. */
static void RainFx_Changed(S32 value) {
    if (value)
        Poly_RequireBlendLUT();
}

static const T_SETTING RainFxSettings[] = {
    /* Recorded: the wind moves the drops and the streak decides where a drop
       meets the ground, both of which reach the shared random stream. Added
       after the header carried every row, so a recording without it predates
       the setting and replays with the 1997 look. */
    {"RainFx", NULL, "gfx_rain_fx", &RainFx, SETTING_TRUTHY, 0, 0, 1, NULL, NULL, RainFx_Changed, NULL,
     "Rain: 0 the 1997 look (opaque one-pixel lines), 1 translucent depth-graded streaks and a gusting wind",
     SETTING_RECORDED, SETTING_BEFORE(0), SETTING_ABSENT_BEFORE},
};
#define RAIN_FX_SETTING_COUNT ((S32)(sizeof(RainFxSettings) / sizeof(RainFxSettings[0])))

/* When lightning last struck, 0 before the first strike this run. */
static U32 s_lastFlash = 0;

static S32 FlashAgeNow(void) {
    return s_lastFlash ? (S32)((TimerRefHR - s_lastFlash) & 0x7FFFFFFF) : -1;
}

static S32 WindNow(void) {
    return RainFx_Wind((S32)(TimerRefHR & 0x7FFFFFFF)) + RainFx_FlashGust(FlashAgeNow());
}

void RainFx_Lightning(void) {
    s_lastFlash = TimerRefHR ? TimerRefHR : 1;
}

/* The clock is read only with the look on: off, the frame's drift is the 1997
   half of the delta and the streak its 1997 length, nothing of the wind. */
S32 RainFx_DriftX(S32 delta) {
    return RainFx ? RainFx_Drift(RainFx, delta, WindNow()) : RainFx_Drift(0, delta, 0);
}

S32 RainFx_DropCount(S32 count) {
    return RainFx_Count(RainFx, count, RainFx_Intensity((S32)(TimerRefHR & 0x7FFFFFFF), FlashAgeNow()), RAIN_POOL);
}

S32 RainFx_DeltaX(S32 dx) {
    return RainFx ? RainFx_StreakX(RainFx, dx, WindNow()) : RainFx_StreakX(0, dx, 0);
}

S32 RainFx_DeltaYZ(S32 d) {
    return RainFx_StreakYZ(RainFx, d);
}

void RainFx_InitNear(T_RAIN *pt) {
    const S32 dx = VueOffsetX - CameraX, dy = VueOffsetY - CameraY, dz = VueOffsetZ - CameraZ;
    const S32 dist = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) + (dz < 0 ? -dz : dz);
    const S32 u = 15 + MyRnd(50); /* hundredths of the way from the eye to the target */
    const S32 lat = RainFx_NearHalfWidth(dist * u / 100, RainScale_ViewWidth());
    S32 x = CameraX + dx * u / 100;
    S32 y = CameraY + dy * u / 100;
    S32 z = CameraZ + dz * u / 100;

    x += MyRnd(2 * lat) - lat;
    z += MyRnd(2 * lat) - lat;
    y += 500 + MyRnd(3000);

    pt->XRain = x;
    pt->YRain = y;
    pt->ZRain = z;
    pt->Timer = 0;
}

/* LineRain's sentinels for a line that showed nothing (3DEXT/LINERAIN.CPP). */
#define RAIN_FX_BOX_MIN (S32)0x7FFFFFFF
#define RAIN_FX_BOX_MAX (S32)0x80000000u

/* The depth window LineRain tests each pixel against: a pixel nearer than the
   streak's depth less RAIN_FX_RANGE / 2 hides it, one within RAIN_FX_RANGE of
   that is the ground it splashes on. */
#define RAIN_FX_RANGE 200

/* How far, in the same depth units, a pixel may be from a splash's impact and
   still be the surface the drop struck: the ring lies on that surface and is
   hidden by anything nearer, and not drawn into a hole deeper than it. */
#define RAIN_FX_RIPPLE_SURFACE 1500

/* Clip the segment to the clip window, moving its ends and depths along it.
   Returns 0 when nothing of it is inside. The window is inclusive on every
   side, as LineRain's is. */
static S32 ClipSegment(S32 *x0, S32 *y0, S32 *z0, S32 *x1, S32 *y1, S32 *z1) {
    const S32 dx = *x1 - *x0, dy = *y1 - *y0, dz = *z1 - *z0;
    S64 t0 = 0, t1 = 65536;
    S32 p[4], q[4], i;

    p[0] = -dx;
    q[0] = *x0 - ClipXMin;
    p[1] = dx;
    q[1] = ClipXMax - *x0;
    p[2] = -dy;
    q[2] = *y0 - ClipYMin;
    p[3] = dy;
    q[3] = ClipYMax - *y0;

    for (i = 0; i < 4; i++) {
        if (p[i] == 0) {
            if (q[i] < 0)
                return 0;
        } else {
            const S64 t = (S64)q[i] * 65536 / p[i];
            if (p[i] < 0) {
                if (t > t0)
                    t0 = t;
            } else {
                if (t < t1)
                    t1 = t;
            }
        }
    }
    if (t0 > t1)
        return 0;

    if (t1 < 65536) {
        *x1 = *x0 + (S32)(((S64)dx * t1 + 32768) >> 16);
        *y1 = *y0 + (S32)(((S64)dy * t1 + 32768) >> 16);
        *z1 = *z0 + (S32)(((S64)dz * t1 + 32768) >> 16);
    }
    if (t0 > 0) {
        *x0 += (S32)(((S64)dx * t0 + 32768) >> 16);
        *y0 += (S32)(((S64)dy * t0 + 32768) >> 16);
        *z0 += (S32)(((S64)dz * t0 + 32768) >> 16);
    }
    /* The 16.16 parameters are exact to the pixel below 32768 pixels of
       length; the clamp keeps a longer segment's ends in the window too. */
    if (*x0 < ClipXMin)
        *x0 = ClipXMin;
    else if (*x0 > ClipXMax)
        *x0 = ClipXMax;
    if (*y0 < ClipYMin)
        *y0 = ClipYMin;
    else if (*y0 > ClipYMax)
        *y0 = ClipYMax;
    if (*x1 < ClipXMin)
        *x1 = ClipXMin;
    else if (*x1 > ClipXMax)
        *x1 = ClipXMax;
    if (*y1 < ClipYMin)
        *y1 = ClipYMin;
    else if (*y1 > ClipYMax)
        *y1 = ClipYMax;
    return 1;
}

/* The translucent streak. One pixel per step of the longer axis, as LineRain
   draws, each column tested against the depth buffer on its own and blended
   into the pixel behind it through the texture filter's mix table. */
static S32 Streak(S32 x0, S32 y0, S32 z0, S32 x1, S32 y1, S32 z1, S32 c) {
    U8 *log = (U8 *)Log;
    S32 flags = 0;
    S32 bx0 = RAIN_FX_BOX_MIN, by0 = RAIN_FX_BOX_MIN, bx1 = RAIN_FX_BOX_MAX, by1 = RAIN_FX_BOX_MAX;
    S32 dx, dy, dz, steps, i, cap, width;
    U8 colour = (U8)RainFx_Brighten(c & 0xFF);

    if (Fill_Flag_Fog == 1)
        colour = Fill_Logical_Palette[colour];

    cap = RainFx_Strength((z0 + z1) / 2);
    width = RainScale_StreakWidth() + RainFx_ExtraWidth((z0 + z1) / 2);

    /* The ramp as LineRain carries it (RainFx_DepthStart / DepthDelta): the
       1997 storm's far drops are on screen through the wrap, and a streak
       straddling the far clip ramps as the ASM's does. Taken after the
       strength and width above measured the depth as it is. */
    dz = RainFx_DepthDelta(z0, z1);
    z0 = RainFx_DepthStart(z0);
    z1 = z0 + dz;

    if (!ClipSegment(&x0, &y0, &z0, &x1, &y1, &z1)) {
        ScreenXMin = bx0;
        ScreenYMin = by0;
        ScreenXMax = bx1;
        ScreenYMax = by1;
        return 0;
    }

    dx = x1 - x0;
    dy = y1 - y0;
    dz = z1 - z0;
    steps = (dx < 0 ? -dx : dx);
    if ((dy < 0 ? -dy : dy) > steps)
        steps = (dy < 0 ? -dy : dy);

    for (i = 0; i <= steps; i++) {
        const S32 x = steps ? x0 + RainFx_Dda(dx, i, steps) : x0;
        const S32 y = steps ? y0 + RainFx_Dda(dy, i, steps) : y0;
        const S32 z = steps ? z0 + RainFx_Dda(dz, i, steps) : z0;
        const U32 row = PTR_TabOffLine[y];
        const S32 q = RainFx_PixelStrength(i, steps, cap);
        S32 k;

        for (k = 0; k < width; k++) {
            const S32 xk = x + k;
            const U32 off = row + (U32)xk;
            S32 d;

            if (xk > ClipXMax)
                break;
            d = (S32)PtrZBuffer[off] - (z - RAIN_FX_RANGE / 2);
            if (d < 0)
                continue;
            if (d < RAIN_FX_RANGE)
                flags |= 2;
            flags |= 1;
            log[off] = (q >= 4) ? colour : PolyBlendLUT[q - 1][(((U32)log[off]) << 8) | colour];
            if (xk < bx0)
                bx0 = xk;
            if (xk > bx1)
                bx1 = xk;
            if (y < by0)
                by0 = y;
            if (y > by1)
                by1 = y;
        }
    }

    ScreenXMin = bx0;
    ScreenYMin = by0;
    ScreenXMax = bx1;
    ScreenYMax = by1;
    return flags;
}

/* A ring pixel: on the struck surface, within the clip, blended at strength q. */
static void RipplePixel(S32 x, S32 y, S32 z, U8 colour, S32 q, S32 *bx0, S32 *by0, S32 *bx1, S32 *by1) {
    U8 *log = (U8 *)Log;
    U32 off;
    S32 d;

    if (x < ClipXMin || x > ClipXMax || y < ClipYMin || y > ClipYMax)
        return;
    off = PTR_TabOffLine[y] + (U32)x;
    d = (S32)PtrZBuffer[off] - z;
    if (d < -RAIN_FX_RIPPLE_SURFACE || d > RAIN_FX_RIPPLE_SURFACE)
        return;
    log[off] = PolyBlendLUT[q - 1][(((U32)log[off]) << 8) | colour];
    if (x < *bx0)
        *bx0 = x;
    if (x > *bx1)
        *bx1 = x;
    if (y < *by0)
        *by0 = y;
    if (y > *by1)
        *by1 = y;
}

void RainFx_Ripple(S32 x, S32 y, S32 z, S32 xp, S32 dt, S32 c, S32 *bx0, S32 *by0, S32 *bx1, S32 *by1) {
    S32 rx, ry, px, py, q;
    S64 rx2, ry2, err;
    U8 colour;

    if (!RainFx || xp < 1)
        return;

    /* The ring widens with the age and rounds to the V's half width three
       times over; flattened to half its width, as the ground is seen. */
    rx = RainFx_RippleRadius(xp, dt);
    ry = rx / 2 + 1;
    q = (dt < 40) ? 3 : 2;
    colour = (U8)RainFx_Brighten(c & 0xFF);
    if (Fill_Flag_Fog == 1)
        colour = Fill_Logical_Palette[colour];
    z &= 0xFFFF;

    /* Midpoint ellipse, the four quadrants from each step. */
    rx2 = (S64)rx * rx;
    ry2 = (S64)ry * ry;
    px = 0;
    py = ry;
    err = ry2 - rx2 * ry + rx2 / 4;
    while (ry2 * px < rx2 * py) {
        RipplePixel(x + px, y + py, z, colour, q, bx0, by0, bx1, by1);
        RipplePixel(x - px, y + py, z, colour, q, bx0, by0, bx1, by1);
        RipplePixel(x + px, y - py, z, colour, q, bx0, by0, bx1, by1);
        RipplePixel(x - px, y - py, z, colour, q, bx0, by0, bx1, by1);
        if (err < 0) {
            err += ry2 * (2 * px + 3);
        } else {
            err += ry2 * (2 * px + 3) - rx2 * (2 * py - 2);
            py--;
        }
        px++;
    }
    err = ry2 * ((S64)px * px + px) + rx2 * ((S64)py * py - py) - rx2 * ry2;
    while (py >= 0) {
        RipplePixel(x + px, y + py, z, colour, q, bx0, by0, bx1, by1);
        RipplePixel(x - px, y + py, z, colour, q, bx0, by0, bx1, by1);
        RipplePixel(x + px, y - py, z, colour, q, bx0, by0, bx1, by1);
        RipplePixel(x - px, y - py, z, colour, q, bx0, by0, bx1, by1);
        if (err > 0) {
            err += rx2 * (3 - 2 * py);
        } else {
            err += rx2 * (3 - 2 * py) + ry2 * (2 * px + 2);
            px++;
        }
        py--;
    }
}

S32 RainFx_Line(S32 x0, S32 y0, S32 z0, S32 x1, S32 y1, S32 z1, S32 c) {
    if (!RainFx)
        return LineRain(x0, y0, z0, x1, y1, z1, c);
    Poly_RequireBlendLUT();
    return Streak(x0, y0, z0, x1, y1, z1, c);
}

void RainFx_ReadConfig(void) {
    Settings_ReadConfig(RainFxSettings, RAIN_FX_SETTING_COUNT);
}

void RainFx_WriteConfig(void) {
    Settings_WriteConfig(RainFxSettings, RAIN_FX_SETTING_COUNT);
}

void RainFx_RegisterCvars(void) {
    Settings_RegisterCvars(RainFxSettings, RAIN_FX_SETTING_COUNT);
}

const T_SETTING *RainFx_Settings(S32 *count) {
    *count = RAIN_FX_SETTING_COUNT;
    return RainFxSettings;
}
