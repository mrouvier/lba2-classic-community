#include "RAIN_SCALE.H"
#include "RES_SWITCH.H" /* Res_BaseDimensions: the resolution the player chose */
#include "SETTINGS.H"
#include "3DEXT/EXTERN.H" /* LineRain */

#include <SVGA/DIRTYBOX.H> /* BoxMovingAdd */
#include <SVGA/SCREENXY.H> /* ScreenXMin.. ScreenYMax: the extent LineRain drew */

/* See RAIN_SCALE.H. */

S32 RainScale = 0;

static const T_SETTING RainScaleSettings[] = {
    /* Recorded: every drop takes three values from the shared random stream when
       it starts and whenever AffRain or ClearImpactRain restarts it, so the
       count is game state.
       Added after the header carried every row, so a recording without it
       predates the setting and replays with the 1997 storm. */
    {"RainScale", NULL, "gfx_rain_scale", &RainScale, SETTING_TRUTHY, 0, 0, 1, NULL, NULL, NULL, NULL,
     "Rain: 0 the 1997 storm (200 drops one pixel wide), 1 its density and streak width on this framebuffer",
     SETTING_RECORDED, SETTING_BEFORE(0), SETTING_ABSENT_BEFORE},
};
#define RAIN_SCALE_SETTING_COUNT ((S32)(sizeof(RainScaleSettings) / sizeof(RainScaleSettings[0])))

/* The resolution the player chose. An interior drawn at a divided size
   (LBA2_ISO_DIV) renders smaller than that, and a storm started there would
   otherwise be sized for the room rather than for the exterior it falls in. */
static void BaseFrame(S32 *w, S32 *h) {
    U32 bw, bh;
    Res_BaseDimensions(&bw, &bh);
    *w = (S32)bw;
    *h = (S32)bh;
}

S32 RainScale_DropCount(void) {
    S32 w, h;
    BaseFrame(&w, &h);
    return RainScale_Count(RainScale, w, h, RAIN_POOL);
}

S32 RainScale_StreakWidth(void) {
    S32 w, h;
    BaseFrame(&w, &h);
    return RainScale_Width(RainScale, h);
}

S32 RainScale_SpanXZ(S32 span) {
    S32 w, h;
    BaseFrame(&w, &h);
    return RainScale_Span(RainScale, span, w, RAIN_BASE_W);
}

S32 RainScale_SpanY(S32 span) {
    S32 w, h;
    BaseFrame(&w, &h);
    return RainScale_Span(RainScale, span, h, RAIN_BASE_H);
}

void RainScale_DrawColumns(S32 x0, S32 y0, S32 z0, S32 x1, S32 y1, S32 z1, S32 c, S32 width, S32 *by0, S32 *bx1,
                           S32 *by1) {
    S32 k;

    for (k = 1; k < width; k++) {
        if (LineRain(x0 + k, y0, z0, x1 + k, y1, z1, c) & 1) {
            if (ScreenYMin < *by0)
                *by0 = ScreenYMin;
            if (ScreenXMax > *bx1)
                *bx1 = ScreenXMax;
            if (ScreenYMax > *by1)
                *by1 = ScreenYMax;
        }
    }
}

static S32 s_coalesce = 0;
static S32 s_boxUsed = 0;
static S32 s_boxX0, s_boxY0, s_boxX1, s_boxY1;

void RainScale_BoxBegin(S32 nbRain, S32 width) {
    s_coalesce = RainScale_Coalesce(nbRain, width);
    s_boxUsed = 0;
}

void RainScale_BoxAdd(S32 x0, S32 y0, S32 x1, S32 y1) {
    if (!s_coalesce) {
        BoxMovingAdd(x0, y0, x1, y1);
        return;
    }
    if (!s_boxUsed) {
        s_boxX0 = x0;
        s_boxY0 = y0;
        s_boxX1 = x1;
        s_boxY1 = y1;
        s_boxUsed = 1;
        return;
    }
    if (x0 < s_boxX0)
        s_boxX0 = x0;
    if (y0 < s_boxY0)
        s_boxY0 = y0;
    if (x1 > s_boxX1)
        s_boxX1 = x1;
    if (y1 > s_boxY1)
        s_boxY1 = y1;
}

void RainScale_BoxFlush(void) {
    if (s_boxUsed)
        BoxMovingAdd(s_boxX0, s_boxY0, s_boxX1, s_boxY1);
    s_boxUsed = 0;
}

void RainScale_ReadConfig(void) {
    Settings_ReadConfig(RainScaleSettings, RAIN_SCALE_SETTING_COUNT);
}

void RainScale_WriteConfig(void) {
    Settings_WriteConfig(RainScaleSettings, RAIN_SCALE_SETTING_COUNT);
}

void RainScale_RegisterCvars(void) {
    Settings_RegisterCvars(RainScaleSettings, RAIN_SCALE_SETTING_COUNT);
}

const T_SETTING *RainScale_Settings(S32 *count) {
    *count = RAIN_SCALE_SETTING_COUNT;
    return RainScaleSettings;
}
