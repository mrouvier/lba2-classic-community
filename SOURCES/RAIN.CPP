#include "C_EXTERN.H"
#include "RAIN_SCALE.H" /* the drop count and streak width for this framebuffer */
#include "RAIN_FX.H"    /* the look: the streak and the wind */

//----------------------------------------------------------------------------
#define RAIN_VX 200
#define RAIN_VY 2500
#define RAIN_WEIGHT 30

//----------------------------------------------------------------------------
#define RAIN_STOP 0

#define RAIN_DELTA_X 128
#define RAIN_DELTA_Y 256
#define RAIN_DELTA_Z 128

T_RAIN TabRain[MAX_RAIN];

/* The pool is sized here from COMMON.H and run from RAIN_SCALE.H; the two
 * cannot share a header, so a pool that drifts from the other refuses to build.
 * The dirty-box list keeps the 1997 storm's share, a box per drop. */
typedef char RainPoolMatchesRainScale[(MAX_RAIN == RAIN_POOL) ? 1 : -1];
typedef char RainBoxesAreTheDrops[(RAIN_BOXES == RAIN_DROPS) ? 1 : -1];

/* How many of the pool are running: RAIN_DROPS for the 1997 storm, more on a
 * bigger framebuffer with RainScale on. Set by InitRain and followed by GereRain,
 * which starts a drop the count gains and simply stops running one it loses. */
static S32 NbRain = 0;

S32 LastTimer = 0;
S32 DeltaRain = 0;

/* Set when the next full redraw only moves the camera smoothly (the Auto
 * camera following the hero). The original camera only moved on a cut, and
 * every cut cleared the splashes. The Auto camera redraws on every frame it
 * moves, so clearing then would erase each splash before its first draw. */
S32 KeepImpactRain = FALSE;

//----------------------------------------------------------------------------
void InitOneRain(T_RAIN *pt) {
    S32 rndy;
    S32 spanxz = RainScale_SpanXZ(30000); /* the box's side, for a view this wide */

    if (RainFx_NearDrop(RainFx, (S32)(pt - TabRain))) {
        RainFx_InitNear(pt); /* the look's near layer, in front of the camera */
        return;
    }

    /* The range above the target, VueOffsetY + 10000 in 1997, scaled whole so
       the box's height grows with the frame's at any altitude. */
    rndy = MyRnd(RainScale_SpanY(VueOffsetY + 10000));

    pt->YRain = VueOffsetY + rndy;

    rndy = rndy / 2 + spanxz / 2;

    pt->XRain = VueOffsetX - rndy + MyRnd(spanxz);
    pt->ZRain = VueOffsetZ - rndy + MyRnd(spanxz);
    pt->Timer = 0;
}

//----------------------------------------------------------------------------
#define RestartOneRain InitOneRain

/*
void	RestartOneRain( T_RAIN *pt )
{
	pt->YRain = VueOffsetY + 10000;
	pt->XRain = VueOffsetX - 10000/2 - 15000 + MyRnd( 30000 ) ;
	pt->ZRain = VueOffsetZ - 10000/2 - 15000 + MyRnd( 30000 ) ;
	pt->Timer = 0 ;
}
*/
//----------------------------------------------------------------------------
// faire init special entre chaque changement de camera qui clear tous les
// impact et qui init les goutes a n'importe quel Y
void InitRain() {
    S32 i;

    NbRain = RainFx_DropCount(RainScale_DropCount());

    for (i = 0; i < NbRain; i++) {
        InitOneRain(&TabRain[i]);
    }

    LastTimer = 0;
}

//----------------------------------------------------------------------------
void ClearImpactRain() {
    S32 i;

    for (i = 0; i < NbRain; i++) {
        if (TabRain[i].Timer) // eclat de gouttes
        {
            InitOneRain(&TabRain[i]);
        }
    }
}

//----------------------------------------------------------------------------
void GereRain() {
    S32 temp;
    S32 i;

    temp = TimerRefHR;
    DeltaRain = LastTimer ? (temp - LastTimer) * 10 : 0;
    LastTimer = temp;

    temp = RainFx_DropCount(RainScale_DropCount());
    for (i = NbRain; i < temp; i++) {
        InitOneRain(&TabRain[i]);
    }
    NbRain = temp;

    for (i = 0; i < NbRain; i++) {
        if (!TabRain[i].Timer) {
            TabRain[i].XRain += RainFx_DriftX(DeltaRain);
            TabRain[i].ZRain += DeltaRain / 2;
            TabRain[i].YRain -= DeltaRain;
        }
    }
}

//----------------------------------------------------------------------------
void AffRain() {
    S32 i, c;
    S32 xp, yp;
    S32 f;
    S32 width;

    UnsetClip();

    width = RainFx_ColumnWidth(RainFx, RainScale_StreakWidth());
    RainScale_BoxBegin(NbRain, width);

    for (i = 0; i < NbRain; i++) {
        if (TabRain[i].Timer) {
            S32 x0, y0, x1, y1;
            S32 x, y, z;
            S32 dt;

            dt = LastTimer - TabRain[i].Timer;

            c = TabRain[i].XRain >> 16;
            x = (S16)(TabRain[i].XRain & 0xFFFF);
            y = TabRain[i].YRain;
            z = TabRain[i].ZRain;

            yp = (RAIN_VY - RAIN_WEIGHT * dt) * dt / 256;
            if (yp < 0) {
                yp = 0;
                xp = RAIN_VX * RAIN_VY / RAIN_WEIGHT / 256;
            } else {
                xp = RAIN_VX * dt / 256;

                yp = (yp * LFactorY) / z;
            }

            xp = (xp * LFactorX) / z;

            x0 = x - xp;
            x1 = x + xp;

            y0 = y - yp;
            y1 = y;

            z = RegleTrois(0, 65535, ClipZFar, z);

            f = RainFx_Line(x, y, z, x0, y0, z, c);
            f |= RainFx_Line(x, y, z, x1, y0, z, c);

            if (f & 1) {
                S32 bx0 = x0, bx1 = RainFx_SplashRight(RainFx, x1, RainScale_StreakWidth(), z), by0 = y0, by1 = y1;

                RainScale_DrawColumns(x, y, z, x0, y0, z, c, width, &by0, &bx1, &by1);
                RainScale_DrawColumns(x, y, z, x1, y0, z, c, width, &by0, &bx1, &by1);
                RainFx_Ripple(x, y, z, xp, dt, c, &bx0, &by0, &bx1, &by1);
                RainScale_BoxAdd(bx0, by0, bx1, by1);
            }

            if (dt && !yp) {
                RestartOneRain(&TabRain[i]);
            }
        } else {
            S32 z0, z1;

            if (TabRain[i].YRain <= RAIN_STOP) {
                RestartOneRain(&TabRain[i]);
            }

            LongWorldRotatePoint(TabRain[i].XRain - RainFx_DeltaX(RAIN_DELTA_X),
                                 TabRain[i].YRain + RainFx_DeltaYZ(RAIN_DELTA_Y),
                                 TabRain[i].ZRain - RainFx_DeltaYZ(RAIN_DELTA_Z));

            if (!LongProjectPoint(X0, Y0, Z0)) {
                continue;
            }

            xp = Xp;
            yp = Yp;
            z0 = RegleTrois(0, 65535, ClipZFar, CameraZr - Z0);

            LongWorldRotatePoint(TabRain[i].XRain,
                                 TabRain[i].YRain,
                                 TabRain[i].ZRain);

            if (!LongProjectPoint(X0, Y0, Z0)) {
                continue;
            }

            Z0 = CameraZr - Z0;

            z1 = RegleTrois(0, 65535, ClipZFar, Z0);

            c = BoundRegleTrois(16 * 3 + 10, 16 * 3 + 3, ClipZFar - StartZFog, Z0);

            f = RainFx_Line(xp, yp, z0, Xp, Yp, z1, c);

            if (f & 1) {
                S32 bx0 = ScreenXMin, by0 = ScreenYMin, bx1 = ScreenXMax, by1 = ScreenYMax;

                RainScale_DrawColumns(xp, yp, z0, Xp, Yp, z1, c, width, &by0, &bx1, &by1);
                RainScale_BoxAdd(bx0, by0, bx1, by1);
            }

            if (f & 2) {
                if (f & 1) {
                    TabRain[i].XRain = ((xp & 0xFFFF) | (c << 16));
                    TabRain[i].YRain = yp;
                    TabRain[i].ZRain = Z0;

                    // eclat des gouttes
                    TabRain[i].Timer = LastTimer;
                } else {
                    RestartOneRain(&TabRain[i]);
                }
            }
        }
    }

    RainScale_BoxFlush();
}

//----------------------------------------------------------------------------
