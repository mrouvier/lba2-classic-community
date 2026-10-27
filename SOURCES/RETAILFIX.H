#ifndef RETAILFIX_H
#define RETAILFIX_H

#include <SYSTEM/ADELINE_TYPES.H>

struct T_SETTING; /* SETTINGS.H; a pointer to a table needs no more */

/* Fixes to bugs the original game has: the 1997 CD releases, not a later port.
 *
 * Each fix is a setting of its own (fix_<name>, on by default), and one master
 * row decides which apply: "Fix retail bugs" in the options menu, fix_retail in
 * the console, RetailFixes in lba2.cfg.
 *
 *   No      every fix off: the original's rules, including fixes added later
 *   Yes     every fix on
 *   Custom  each fix's own setting decides
 *
 * Yes and No never write the per-fix settings, so going back to Custom brings
 * the player's choices back. Changing a fix's setting at runtime (console,
 * socket) switches the master to Custom, because under Yes or No the change
 * would otherwise be ignored without a word. A replay's borrow does not.
 *
 * A fix site asks one thing, RetailFix_On. Adding a fix is an enum entry, a row
 * in RETAILFIX.CPP and an `if` at the site. Which issues get a fix here rather
 * than an option of their own: docs/knowledge/decisions/a-retail-fix-is-a-switch-under-one-row.md. */

typedef enum {
    RETAIL_FIX_FOUND_OBJ_WALK,    /* a direction held through the found-item popup sticks */
    RETAIL_FIX_TERRAIN_SEAMS,     /* hairline seams between the outdoor ground's cubes */
    RETAIL_FIX_LIGHTNING_CADENCE, /* the lightning strobe runs at half the frame rate */
    RETAIL_FIX_COUNT
} T_RETAIL_FIX;

#define RETAIL_FIXES_NO 0
#define RETAIL_FIXES_YES 1
#define RETAIL_FIXES_CUSTOM 2

/* The master row, for the options menu's label. */
extern S32 RetailFixes;

/* Whether a fix applies now. */
extern S32 RetailFix_On(T_RETAIL_FIX fix);

/* The options menu's row: the value it moves to when selected, written through
 * the setting so it is coerced and persisted like any other. */
extern void RetailFix_MenuSelect(void);

/* The cfg keys, the console cvars and the table, for the surfaces that serve
 * every setting (CONFIG_FILE.CPP, CONSOLE_CMD.CPP), as FOLLOWCAM.H's are. */
extern void RetailFix_ReadConfig(void);
extern void RetailFix_WriteConfig(void);
extern void RetailFix_RegisterCvars(void);
extern const T_SETTING *RetailFix_Settings(S32 *count);

/* The arithmetic, over plain values and free of engine state, so the host test
 * in tests/settings covers it without booting anything. */

/* What a fix site gets, from the master row and the fix's own row. */
static inline S32 RetailFix_Effective(S32 master, S32 fixOn) {
    if (master == RETAIL_FIXES_CUSTOM)
        return fixOn ? 1 : 0;
    return master == RETAIL_FIXES_NO ? 0 : 1;
}

/* The lightning strobe's cadence. The original toggles the flash palette on
 * every frame it draws inside the strobe's window, so the strobe runs at half
 * the frame rate: 10 Hz at 1997's 20 fps, 30 Hz under a 60 Hz vsync. With the
 * fix a toggle waits RETAIL_FIX_LIGHTNING_MS since the last, which is the 1997
 * cadence at any frame rate. `last` is the time of the last toggle and is
 * updated when one happens. */
#define RETAIL_FIX_LIGHTNING_MS 50
static inline S32 RetailFix_LightningToggle(S32 fixOn, U32 now, U32 *last) {
    if (fixOn && now - *last < RETAIL_FIX_LIGHTNING_MS)
        return 0;
    *last = now;
    return 1;
}

/* The menu row's cycle: Yes, No, then Custom only when some fix's own setting
 * is off its default, so Custom means something; back to Yes. */
static inline S32 RetailFix_NextMenuValue(S32 master, S32 anyFixOffDefault) {
    switch (master) {
    case RETAIL_FIXES_YES:
        return RETAIL_FIXES_NO;
    case RETAIL_FIXES_NO:
        return anyFixOffDefault ? RETAIL_FIXES_CUSTOM : RETAIL_FIXES_YES;
    default:
        return RETAIL_FIXES_YES;
    }
}

#endif /* RETAILFIX_H */
