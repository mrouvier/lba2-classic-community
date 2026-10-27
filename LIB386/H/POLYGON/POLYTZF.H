
#pragma once
#include <SYSTEM/ADELINE_TYPES.H>

// -----------------------------------------------------------------------------
#ifdef __cplusplus
extern "C" {
#endif

/* Ordered dither on the fog band of the fillers below, the polygon type the
   sea and the sky are drawn with (POLY_TEXTURE_Z_FOG, DRAWSKY.CPP). They take
   the band from the depth as they draw, once a 32-pixel span (the land takes
   one per polygon): the top four bits of the span's fog value (fogDist *
   Fill_Fog_Factor >> 8), sixteen bands between StartZFog and the far clip, and
   a flat sea seen from above crosses their edges as hard horizontal lines. The
   four bits under the band say how far into it the span is, and dithering on
   them turns each line into a gradient. 0 = off and bit-exact with the
   original. */
extern S32 PolyFogDither;

/* The CLUT offset of the fog band for the pixel at framebuffer column px and
   row py, from the span's raw fog value. Centred on the band: the value is
   taken half a band back, so a span in the middle of its band keeps the
   original's band at every pixel, a span on a band's edge is half one band and
   half the other, and the sea's tone is on average the original's. The band
   is then bits 12..15 of that, promoted one band when bits 8..11 beat the
   pixel's 4x4 Bayer threshold (the GTEX_ShadeRow matrix, POLYGTEX.CPP, in
   sixteenths), so a value f/16 into its band takes the next at f of every 16
   pixels. Only the low 16 bits are read, as the original's mask reads them.
   Saturates at band 15: the fillers index PtrCLUTFog + 12*256 + band over the
   16 bands of the fog CLUT, so there is no band after it. */
static inline U32 PolyTZF_FogDitherRow(U32 raw, U32 px, U32 py) {
    static const U8 bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    const U32 v = raw & 0xFFFF;
    const U32 c = (v >= 0x800) ? v - 0x800 : 0;
    U32 band = c & 0xF000;
    if (band < 0xF000 && ((c >> 8) & 15) > bayer[((py & 3) << 2) | (px & 3)])
        band += 0x1000;
    return band;
}

S32 Filler_TextureZFogSmooth(U32 nbLines, U32 fillCurXMin, U32 fillCurXMax);
S32 Filler_TextureZFogSmoothZBuf(U32 nbLines, U32 fillCurXMin, U32 fillCurXMax);
S32 Filler_TextureZFogSmoothNZW(U32 nbLines, U32 fillCurXMin, U32 fillCurXMax);

#ifdef __cplusplus
}
#endif
