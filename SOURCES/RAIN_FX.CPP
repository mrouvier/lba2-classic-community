#include "RAIN_FX.H"
#include "C_EXTERN.H"   /* VueOffsetX/Y/Z, the view's target; Rnd */
#include "EXTFOV.H"     /* ExtFov_ViewWidth: how wide the view is, in 480-line pixels */
#include "RAIN_SCALE.H" /* RainScale_StreakWidth and RAIN_POOL */
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

static const T_SETTING RainFxSettings[] = {
    /* Recorded: the wind moves the drops and the streak decides where a drop
       meets the ground, both of which reach the shared random stream. Added
       after the header carried every row, so a recording without it predates
       the setting and replays with the 1997 look. */
    {"RainFx", NULL, "gfx_rain_fx", &RainFx, SETTING_TRUTHY, 0, 0, 1, NULL, NULL, NULL, NULL,
     "Rain: 0 the 1997 look (opaque one-pixel lines), 1 translucent depth-graded streaks and a gusting wind",
     SETTING_RECORDED, SETTING_BEFORE(0), SETTING_ABSENT_BEFORE},
};
#define RAIN_FX_SETTING_COUNT ((S32)(sizeof(RainFxSettings) / sizeof(RainFxSettings[0])))

static S32 WindNow(void) {
    return RainFx_Wind((S32)(TimerRefHR & 0x7FFFFFFF));
}

S32 RainFx_DriftX(S32 delta) {
    return RainFx_Drift(RainFx, delta, WindNow());
}

S32 RainFx_DropCount(S32 count) {
    return RainFx_Count(RainFx, count, RAIN_POOL);
}

S32 RainFx_DeltaX(S32 dx) {
    return RainFx_StreakX(RainFx, dx, WindNow());
}

S32 RainFx_DeltaYZ(S32 d) {
    return RainFx_StreakYZ(RainFx, d);
}

void RainFx_InitNear(T_RAIN *pt) {
    const S32 dx = VueOffsetX - CameraX, dy = VueOffsetY - CameraY, dz = VueOffsetZ - CameraZ;
    const S32 dist = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) + (dz < 0 ? -dz : dz);
    const S32 u = 15 + MyRnd(50); /* hundredths of the way from the eye to the target */
    const S32 lat = RainFx_NearHalfWidth(dist * u / 100, ExtFov_ViewWidth(ExteriorFov, (S32)ModeDesiredX, (S32)ModeDesiredY));
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

    /* LineRain shifts each depth into the high half of a 32-bit register, so a
       depth past the far clip (AffRain scales it past 65535) wraps to a near one
       and the drop is drawn over whatever is there. The 1997 storm's far drops
       are on screen through that, so the streak keeps the wrap, after the
       strength and width above took the depth as it is. */
    z0 &= 0xFFFF;
    z1 &= 0xFFFF;

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
        const S32 x = steps ? x0 + (S32)(((S64)dx * i * 2 + steps) / (2 * steps)) : x0;
        const S32 y = steps ? y0 + (S32)(((S64)dy * i * 2 + steps) / (2 * steps)) : y0;
        const S32 z = steps ? z0 + (S32)(((S64)dz * i * 2 + steps) / (2 * steps)) : z0;
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
