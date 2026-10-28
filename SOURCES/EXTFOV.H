#ifndef EXTFOV_H
#define EXTFOV_H

#include <SYSTEM/ADELINE_TYPES.H>

struct T_SETTING; /* SETTINGS.H; a pointer to a table needs no more */

/* The exterior view at any framebuffer height.
 *
 * The exterior projection has one focal, ChampX / ChampZ = 600 (3DEXT/VAR_EXT.CPP),
 * at every resolution, so a pixel covers the same piece of the world at 1080
 * lines as at 480, and a taller frame shows more world rather than more detail:
 * at 1920x1080 the view is 116 by 84 degrees against the original's 56 by 44,
 * the extra height spent on sky and distant cube edges, and the hero a smaller
 * part of the frame.
 *
 * ExteriorFov keeps the original's vertical view instead. On, the focal follows
 * the height, 600 * h / 480, so the frame shows the 1997 height of world and
 * only the width grows with the aspect ratio; detail grows with the resolution,
 * 2.25 times at 1080 lines. Three things that compensated the taller view stand
 * down with it. The near clip, moved in with the height so the ground a taller
 * frustum reached was not culled, is back at CLIP_NEAR. The Auto camera's HD
 * recompose (cam_hd), which pulled the boom in to fill the frame, reads the
 * view's height rather than the frame's and does nothing. The classic camera's
 * off-screen margins, in pixels, follow the focal so the snap fires at the 1997
 * distance from the edge. Off, the default, is byte-identical to the engine
 * without the setting; at 480 lines the setting changes nothing either way.
 *
 * Recorded: the original reads what is on screen (the lightning spell strikes
 * the enemies drawn, a life script tests a drawn actor, the classic camera
 * snaps on the hero's projected position), so the view is game state at any
 * height but 480. Sprites follow the focal like the geometry around them
 * (CalculeScaleFactorSprite in EXTFUNC.CPP); particle dots, text and the HUD,
 * drawn in pixels, stay as they are. */

#define EXTFOV_BASE_H 480

/* 0: the fixed focal. 1: the focal follows the height; the vertical view is the original's. */
extern S32 ExteriorFov;

/* The arithmetic, over plain values and free of engine state, so the host test
 * in tests/extfov covers it without booting anything. */

/* The focal for a frame h lines tall. */
static inline S32 ExtFov_Focal(S32 lock, S32 focal, S32 h) {
    return lock ? focal * h / EXTFOV_BASE_H : focal;
}

/* The near clip for a frame h lines tall. Without the lock a taller frustum
 * reaches the ground nearer the camera, and the terrain culls whole cells within
 * the near plane (MaskVisible in 3DEXT/TERRAIN.CPP), so the plane moves in with
 * the height to stay where it was on screen; with the lock the frustum is the
 * original's and so is the plane. Exact at 480 and below either way. */
static inline S32 ExtFov_NearClip(S32 lock, S32 clipNear, S32 h) {
    if (lock || h <= EXTFOV_BASE_H)
        return clipNear;
    return clipNear * EXTFOV_BASE_H / h;
}

/* The frame measured in the view's pixels, 480 lines tall under the lock, for
 * whatever reasons about how much world is in view rather than how many pixels
 * show it. */
static inline S32 ExtFov_ViewHeight(S32 lock, S32 h) {
    return lock ? EXTFOV_BASE_H : h;
}

static inline S32 ExtFov_ViewWidth(S32 lock, S32 w, S32 h) {
    return (lock && h > 0) ? w * EXTFOV_BASE_H / h : w;
}

/* px, a distance in 1997 pixels, in this frame's pixels: under the lock a world
 * distance spans h / 480 times as many of them. */
static inline S32 ExtFov_Pixels(S32 lock, S32 px, S32 h) {
    return lock ? px * h / EXTFOV_BASE_H : px;
}

/* The same for the live framebuffer. */
extern S32 ExtFov_FocalNow(S32 focal);
extern S32 ExtFov_NearClipNow(S32 clipNear);
extern S32 ExtFov_ViewHeightNow(void);

/* The projection set again for a change the console or the menu made, once per
 * MainLoop frame after its input poll, outside any modal that owns the
 * projection for its own 3D. */
extern void ExtFov_ApplyPending(void);

/* A pixel margin of the classic camera's off-screen snap: px scaled for an
 * exterior frame under the lock, px as it is otherwise (an interior's camera is
 * isometric and not concerned). */
extern S32 ExtFov_Margin(S32 px, S32 exterior);

/* The cfg key, the console cvar and the table, for the surfaces that serve
 * every setting (CONFIG_FILE.CPP, CONSOLE_CMD.CPP), as RETAILFIX.H's are. */
extern void ExtFov_ReadConfig(void);
extern void ExtFov_WriteConfig(void);
extern void ExtFov_RegisterCvars(void);
extern const T_SETTING *ExtFov_Settings(S32 *count);

#endif /* EXTFOV_H */
