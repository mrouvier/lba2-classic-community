#include "EXTFOV.H"
#include "C_EXTERN.H" /* CubeMode, FirstTime, PtrInit3DView: the view is set again on a change */
#include "SETTINGS.H"

#include <SVGA/SCREEN.H> /* ModeDesiredY, the frame the projection is set for */

/* See EXTFOV.H. */

S32 ExteriorFov = 0;

/* The value the live exterior projection was last set from, so a write of the
   same value (a replay's borrow of 0 over the player's 0 and its return, or the
   console repeating itself) sets nothing again: the view is already that one. */
static S32 s_applied = 0;

/* A change waits for MainLoop to set the view (ExtFov_ApplyPending): the console
   can run inside a modal that owns the projection for its own 3D, the holomap
   or an inventory icon, and that modal restores the exterior's view when it
   exits. The redraw is asked for here, since the full redraw a projection
   change needs is what the modal's exit draws into. */
static S32 s_pending = 0;

/* The projection is set at each exterior cube's start (Init3DExtGame) and on a
   resolution switch, from the setting; an interior's camera is isometric, and
   its next exterior reads the setting on entry. */
static void ExtFov_Changed(S32 value) {
    if (value == s_applied)
        return;
    if (CubeMode == CUBE_EXTERIEUR) {
        s_pending = 1;
        FirstTime = AFF_ALL_FLIP;
    }
}

void ExtFov_ApplyPending(void) {
    if (!s_pending)
        return;
    s_pending = 0;
    if (CubeMode == CUBE_EXTERIEUR && PtrInit3DView != NULL)
        PtrInit3DView();
}

static const T_SETTING ExtFovSettings[] = {
    /* Recorded: the original reads what is on screen, so the view decides game
       state at any height but 480. Added after the header carried every row, so
       a recording without it predates the setting and replays on the fixed
       focal. */
    {"ExteriorFov", NULL, "gfx_fov", &ExteriorFov, SETTING_TRUTHY, 0, 0, 1, NULL, NULL, ExtFov_Changed, NULL,
     "Exterior view: 0 the fixed focal (a taller frame shows more world), 1 keep the 1997 vertical view (detail "
     "grows with the height)",
     SETTING_RECORDED, SETTING_BEFORE(0), SETTING_ABSENT_BEFORE},
};
#define EXTFOV_SETTING_COUNT ((S32)(sizeof(ExtFovSettings) / sizeof(ExtFovSettings[0])))

S32 ExtFov_FocalNow(S32 focal) {
    s_applied = ExteriorFov;
    return ExtFov_Focal(ExteriorFov, focal, (S32)ModeDesiredY);
}

S32 ExtFov_NearClipNow(S32 clipNear) {
    return ExtFov_NearClip(ExteriorFov, clipNear, (S32)ModeDesiredY);
}

S32 ExtFov_ViewHeightNow(void) {
    return ExtFov_ViewHeight(ExteriorFov, (S32)ModeDesiredY);
}

S32 ExtFov_Margin(S32 px, S32 exterior) {
    return exterior ? ExtFov_Pixels(ExteriorFov, px, (S32)ModeDesiredY) : px;
}

void ExtFov_ReadConfig(void) {
    Settings_ReadConfig(ExtFovSettings, EXTFOV_SETTING_COUNT);
}

void ExtFov_WriteConfig(void) {
    Settings_WriteConfig(ExtFovSettings, EXTFOV_SETTING_COUNT);
}

void ExtFov_RegisterCvars(void) {
    Settings_RegisterCvars(ExtFovSettings, EXTFOV_SETTING_COUNT);
}

const T_SETTING *ExtFov_Settings(S32 *count) {
    *count = EXTFOV_SETTING_COUNT;
    return ExtFovSettings;
}
