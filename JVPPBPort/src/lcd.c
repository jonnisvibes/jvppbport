// JVPPBPort: the LCD filter (Options -> SCREEN -> FILTER). Makes the picture
// look like a handheld's LCD instead of sharp square pixels:
//   OFF  sharp pixels, as before.
//   LCD  every game pixel is a little LCD cell: red, green and blue
//        subpixel stripes with thin dark gaps between cells.
//   GBA  the LCD cells plus the Game Boy Advance screen's own colors (paler
//        and darker than a PC's) and a little of its slow response
//        (moving things leave a short trail).
//
// How it's drawn (SDL's renderer only, so it works on Direct3D, OpenGL and
// Android's OpenGL ES alike; no shaders):
// 1. GBA colors, the trail and the color correction (see 5) are done on the
//    CPU, on the game's small canvas (a few lookups per pixel), before it
//    becomes the window's picture.
// 2. The picture is copied k times bigger (k = the window's scale rounded
//    up) into a texture, then shrunk to the window with smoothing ("sharp
//    bilinear"): every game pixel comes out the same size, also at scales
//    like 2.4x, with soft edges only where pixels meet.
// 3. The LCD cells are two thin masks multiplied over the game's area: one
//    row of window pixels (the subpixel stripes and the gaps between
//    columns, per color) stretched down, and one column (the gaps between
//    rows) stretched across. Each mask pixel holds how much of its window
//    pixel the stripe or gap covers, so the grid stays even at any scale
//    (no beat patterns from rounding).
// 4. The picture is added once more, faintly, to win back the brightness
//    the masks took (as LCD shaders in emulators do).
// 5. Step 4 can't make a channel brighter than full, so on its own it would
//    clip each color's own stripe in bright pixels and wash out pale colors
//    (the boards' peach and light blue lost about a quarter of their
//    color). So the masks are measured once (how bright each channel comes
//    out on average for every input level) and each pixel is first made
//    darker by just enough: all three channels by the same factor, from
//    the brightest one (so its hue and saturation stay), and only where
//    needed (bright pixels; mid-tones and darks come out as without the
//    filter). Whites end as bright as before.
// The grid is fixed to the window, like a real screen's: tilt wobble moves
// the picture under it. Menus outside the game's canvas (Android's cabinet)
// only get step 2. Small windows (under ~2x) get a fainter grid, since
// there's no room to draw cells there.
//
// Test: JVPPBPORT_LCD=off|lcd|gba picks the filter without Options (not
// saved). JVPPBPORT_SHOTS also saves win_<frame>.bmp (the window's real
// pixels) while a filter is on.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "jvppbport.h"

// The cell (a fraction of a game pixel).
#define GAP_X 0.14f        // dark gap between columns
#define GAP_Y 0.16f        // dark gap between rows
#define GAP_MIN_PX 0.6f    // but at least this much of a window pixel (small scales)
#define GAP_MAX 0.35f
#define GAP_LEVEL 0.0f     // how much light the gaps let through
#define STRIPE_LEAK 0.30f  // light of the other two colors in a subpixel stripe
#define BRIGHTNESS 1.0f    // average brightness after the masks (1 = as without)
#define SAMPLES 32         // per window pixel, when measuring how much a stripe covers it
// GBA colors: the screen's colors as measured by Pokefan531 (the numbers
// behind the "gba-color" shaders in RetroArch and other emulators), in linear
// light; DARKEN is how much darker than a PC's mid-tones the unlit GBA
// screen is (0 = none).
#define GBA_DARKEN 0.35
#define GBA_LUMINANCE 0.94
static const double sGbaColors[3][3] = {
    {0.82, 0.24, -0.06},   // red   from R, G, B
    {0.125, 0.665, 0.21},  // green
    {0.195, 0.075, 0.73},  // blue
};
#define TRAIL 96           // of 256: how much of the last frame stays (GBA)
#define KNEE 0.7           // colors up to this brightness come out unchanged (step 5)

static const char *const sNames[] = {"Off", "Lcd", "Gba"};
static int sMode = -1;

static int ModeFromName(const char *s)
{
    int i;

    for (i = 0; s && i < PORT_LCD_COUNT; i++)
        if (SDL_strcasecmp(s, sNames[i]) == 0)
            return i;
    return PORT_LCD_OFF;
}

int PortLcdFilter(void)
{
    if (sMode < 0)
        sMode = ModeFromName(getenv("JVPPBPORT_LCD") ? getenv("JVPPBPORT_LCD")
                                                     : PortSettingGet("Display", "LcdFilter"));
    return sMode;
}

void PortSetLcdFilter(int mode)
{
    if (mode < 0 || mode >= PORT_LCD_COUNT || mode == PortLcdFilter())
        return;
    sMode = mode;
    if (getenv("JVPPBPORT_LCD"))
        return;  // a test's choice isn't kept
    PortSettingSet("Display", "LcdFilter", sNames[mode]);
    PortSettingsSave();
}

// ---- 1. GBA colors and trail (CPU, on the canvas) ----

static uint32_t *sGbaLut;   // 6 bits per color in -> 0xFFRRGGBB out
static uint32_t *sOut;      // the canvas after it (also the last frame, for the trail)
static int sOutW, sOutH, sHaveLast;

static void MakeGbaLut(void)
{
    int i, c;
    double gamma = 2.2 + GBA_DARKEN;

    sGbaLut = malloc(64 * 64 * 64 * sizeof(*sGbaLut));
    for (i = 0; i < 64 * 64 * 64; i++)
    {
        double in[3], out;
        uint32_t argb = 0xFF000000;

        in[0] = pow(((i >> 12) & 63) / 63.0, gamma);
        in[1] = pow(((i >> 6) & 63) / 63.0, gamma);
        in[2] = pow((i & 63) / 63.0, gamma);
        for (c = 0; c < 3; c++)
        {
            out = (sGbaColors[c][0] * in[0] + sGbaColors[c][1] * in[1] + sGbaColors[c][2] * in[2]) * GBA_LUMINANCE;
            out = out < 0 ? 0 : out > 1 ? 1 : out;
            argb |= (uint32_t)(pow(out, 1 / 2.2) * 255 + 0.5) << (16 - 8 * c);
        }
        sGbaLut[i] = argb;
    }
}

// Step 5: the input level that comes out at target t/1023 (per channel),
// and the factor for a pixel whose brightest channel is m, times
// 1023/255*256 (so (c * sKnee[m]) >> 8 indexes sInv). Made by MakeCorrection.
static uint8_t sInv[3][1024];
static uint16_t sKnee[256];
static int sHaveCorrection;

static uint32_t Correct(uint32_t c)
{
    int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    int m = r > g ? (r > b ? r : b) : (g > b ? g : b), k = sKnee[m];

    return 0xFF000000 | (uint32_t)sInv[0][(r * k) >> 8] << 16 | (uint32_t)sInv[1][(g * k) >> 8] << 8
           | sInv[2][(b * k) >> 8];
}

const uint32_t *PortLcdProcess(const uint32_t *canvas, int w, int h)
{
    int i, n = w * h, gba = PortLcdFilter() == PORT_LCD_GBA;

    if (PortLcdFilter() == PORT_LCD_OFF || (!gba && !sHaveCorrection))
    {
        sHaveLast = 0;
        return canvas;
    }
    if (gba && !sGbaLut)
        MakeGbaLut();
    if (!sOut || w * h > sOutW * sOutH)
    {
        free(sOut);
        sOut = malloc((size_t)n * sizeof(*sOut));
    }
    if (w != sOutW || h != sOutH)
        sHaveLast = 0;  // another screen size: no trail from it
    sOutW = w;
    sOutH = h;
    for (i = 0; i < n; i++)
    {
        uint32_t c = canvas[i];
        uint32_t v = gba ? sGbaLut[((c >> 6) & 0x3F000) | ((c >> 4) & 0xFC0) | ((c >> 2) & 0x3F)] : c;

        if (sHaveCorrection)
            v = Correct(v);
        if (gba && sHaveLast)
        {
            uint32_t last = sOut[i], mixed = 0xFF000000;
            int k;

            for (k = 0; k < 24; k += 8)
            {
                int a = (v >> k) & 0xFF, b = (last >> k) & 0xFF;
                mixed |= (uint32_t)(a + (((b - a) * TRAIL) >> 8)) << k;
            }
            v = mixed;
        }
        sOut[i] = v;
    }
    sHaveLast = gba;
    return sOut;
}

// ---- 2-4. Drawing (GPU) ----

static SDL_Texture *sBig;    // the picture, k times bigger
static SDL_Texture *sCols;   // outW x 1: stripes and column gaps
static SDL_Texture *sRows;   // 1 x outH: row gaps
static int sBigW, sBigH, sK;
static int sMaskW, sMaskH, sMaskMode;
static float sMaskScale, sMaskX, sMaskY, sAdd;
static int sBroken;          // the renderer can't do it (no render targets): plain pixels

static float sGapX = GAP_X, sGapY = GAP_Y;  // for the window's scale (MakeMasks)

// The cell's light at position f (0..1 across one game pixel) for color c,
// at full strength. The gaps are at the cell's right and bottom edges (not
// split over both sides: at 2x that would darken both window pixels of a
// cell alike, and no grid would show).
static float Column(float f, int c)
{
    float lit = 1 - sGapX;

    if (f >= lit)
        return GAP_LEVEL;
    return (int)(f / lit * 3) == c ? 1.0f : STRIPE_LEAK;
}

static float Row(float f)
{
    return f >= 1 - sGapY ? GAP_LEVEL : 1.0f;
}

// Average of a cell profile over window pixel [p, p+1), whose game pixels
// start at `origin` and are `scale` window pixels big.
static float Cover(int p, float origin, float scale, int c, int across)
{
    float sum = 0;
    int i;

    for (i = 0; i < SAMPLES; i++)
    {
        float u = (p + (i + 0.5f) / SAMPLES - origin) / scale;
        float f = u - floorf(u);
        sum += across ? Column(f, c) : Row(f);
    }
    return sum / SAMPLES;
}

static uint8_t Byte(float v)
{
    return (uint8_t)(v < 0 ? 0 : v > 1 ? 255 : v * 255 + 0.5f);
}

// Step 5: measures how bright each channel comes out of steps 2-4 for every
// input level (on average over the window, from the masks as the GPU gets
// them: whole bytes), and makes the tables that undo the clipping.
static void MakeCorrection(const uint32_t *cols, int outW, const uint32_t *rows, int outH, uint8_t add)
{
    static uint32_t colCount[3][256], rowCount[256];
    static float out[3][256];
    float reach = 1, knee;
    int x, y, c, v, t, a, b;

    memset(colCount, 0, sizeof(colCount));
    memset(rowCount, 0, sizeof(rowCount));
    for (x = 0; x < outW; x++)
        for (c = 0; c < 3; c++)
            colCount[c][(cols[x] >> (16 - 8 * c)) & 0xFF]++;
    for (y = 0; y < outH; y++)
        rowCount[rows[y] & 0xFF]++;
    for (c = 0; c < 3; c++)
    {
        for (v = 0; v < 256; v++)
            out[c][v] = 0;
        for (a = 0; a < 256; a++)
            for (b = 0; colCount[c][a] && b < 256; b++)
            {
                float gain = a / 255.0f * (b / 255.0f) + add / 255.0f;
                float weight = (float)colCount[c][a] * rowCount[b] / ((float)outW * outH);

                if (!rowCount[b])
                    continue;
                for (v = 0; v < 256; v++)
                {
                    float o = v / 255.0f * gain;
                    out[c][v] += weight * (o > 1 ? 1 : o);
                }
            }
        if (out[c][255] < reach)
            reach = out[c][255];
        // Inverse: the lowest level reaching t (in between levels by line).
        for (t = 0, v = 0; t < 1024; t++)
        {
            float target = t / 1023.0f;

            while (v < 255 && out[c][v + 1] < target)
                v++;
            if (v == 255 || out[c][v + 1] <= out[c][v])
                sInv[c][t] = v;
            else
            {
                float f = (target - out[c][v]) / (out[c][v + 1] - out[c][v]);
                f = f < 0 ? 0 : f > 1 ? 1 : f;
                sInv[c][t] = (uint8_t)(v + f + 0.5f);
            }
        }
    }
    // Brightness m comes out as m up to `knee`, then bends smoothly down to
    // `reach` at full (a curve with slope 1 at the knee; the knee is moved
    // down if needed so the curve keeps rising).
    knee = 2 * reach - 1 < KNEE ? 2 * reach - 1 : KNEE;
    for (v = 0; v < 256; v++)
    {
        float m = v / 255.0f, to = m * reach;

        if (knee <= 0)
            to = m * reach;
        else if (m <= knee)
            to = m;
        else
        {
            float s = (m - knee) / (1 - knee), bend = (1 - knee) - (reach - knee);
            to = knee + (1 - knee) * s - bend * s * s;
        }
        sKnee[v] = (uint16_t)(v ? to / m * (1023.0f / 255.0f) * 256 + 0.5f : 1023.0f / 255.0f * 256);
    }
}

// Makes the two masks for a window of outW x outH whose game pixels start at
// (x0, y0) and are `scale` big.
static void MakeMasks(SDL_Renderer *renderer, int outW, int outH, float x0, float y0, float scale)
{
    uint32_t *cols = malloc(outW * 4), *rows = malloc(outH * 4);
    // How strong the cells are: none below 1.5x, full from 2x; the subpixel
    // stripes only from 2.5x (full from 5x): at small sizes they'd only add
    // colored fringes. Gaps are at least GAP_MIN_PX wide, so they still
    // show as lines at 2x.
    float grid = (scale - 1.5f) / 0.5f, stripes = (scale - 2.5f) / 2.5f;

    sGapX = GAP_MIN_PX / scale > GAP_X ? GAP_MIN_PX / scale : GAP_X;
    sGapY = GAP_MIN_PX / scale > GAP_Y ? GAP_MIN_PX / scale : GAP_Y;
    sGapX = sGapX > GAP_MAX ? GAP_MAX : sGapX;
    sGapY = sGapY > GAP_MAX ? GAP_MAX : sGapY;
    double sumCol = 0, sumRow = 0;
    int x, y, c;

    grid = grid < 0 ? 0 : grid > 1 ? 1 : grid;
    stripes = stripes < 0 ? 0 : stripes > 1 ? 1 : stripes;
    for (x = 0; x < outW; x++)
    {
        uint32_t px = 0xFF000000;

        for (c = 0; c < 3; c++)
        {
            // With weak stripes, the stripe of color c and the other two
            // blend toward one gray cell.
            float own = Cover(x, x0, scale, c, 1);
            float neutral = (Cover(x, x0, scale, 0, 1) + Cover(x, x0, scale, 1, 1) + Cover(x, x0, scale, 2, 1))
                            / (1 + 2 * STRIPE_LEAK);
            float v = neutral + (own - neutral) * stripes;
            v = 1 - grid * (1 - v);
            px |= (uint32_t)Byte(v) << (16 - 8 * c);
            sumCol += v;
        }
        cols[x] = px;
    }
    for (y = 0; y < outH; y++)
    {
        float v = 1 - grid * (1 - Cover(y, y0, scale, 0, 0));
        rows[y] = 0xFF000000 | 0x010101u * Byte(v);
        sumRow += v;
    }
    // The faint second copy brings the average back to BRIGHTNESS.
    sAdd = BRIGHTNESS - (float)(sumCol / (3.0 * outW) * (sumRow / outH));
    sAdd = sAdd < 0 ? 0 : sAdd > 1 ? 1 : sAdd;
    MakeCorrection(cols, outW, rows, outH, Byte(sAdd));

    if (sCols)
        SDL_DestroyTexture(sCols);
    if (sRows)
        SDL_DestroyTexture(sRows);
    sCols = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, outW, 1);
    sRows = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 1, outH);
    if (sCols && sRows)
    {
        SDL_UpdateTexture(sCols, NULL, cols, outW * 4);
        SDL_UpdateTexture(sRows, NULL, rows, 4);
        SDL_SetTextureBlendMode(sCols, SDL_BLENDMODE_MOD);
        SDL_SetTextureBlendMode(sRows, SDL_BLENDMODE_MOD);
        SDL_SetTextureScaleMode(sCols, SDL_ScaleModeNearest);
        SDL_SetTextureScaleMode(sRows, SDL_ScaleModeNearest);
    }
    free(cols);
    free(rows);
    sMaskW = outW;
    sMaskH = outH;
    sMaskScale = scale;
    sMaskX = x0;
    sMaskY = y0;
}

static int MakeBig(SDL_Renderer *renderer, int w, int h, int k)
{
    SDL_RendererInfo info;

    if (sBig && sBigW == w * k && sBigH == h * k)
        return 1;
    if (sBig)
        SDL_DestroyTexture(sBig);
    sBig = NULL;
    if (SDL_GetRendererInfo(renderer, &info) != 0 || !(info.flags & SDL_RENDERER_TARGETTEXTURE))
        return 0;
    sBig = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w * k, h * k);
    if (!sBig)
        return 0;
    SDL_SetTextureScaleMode(sBig, SDL_ScaleModeLinear);
    sBigW = w * k;
    sBigH = h * k;
    sK = k;
    return 1;
}

// Draws `picture` (w x h, nearest-pixel texture) with the filter, at
// (x0, y0) in the window's pixels, `scale` window pixels per picture pixel;
// the game's canvas is the rectangle (gx, gy, gw, gh) of it. Returns 0 if
// the filter is off (or can't be drawn): the caller draws as usual then.
int PortLcdDraw(SDL_Renderer *renderer, SDL_Texture *picture, int w, int h, float x0, float y0, float scale,
                int gx, int gy, int gw, int gh)
{
    int outW, outH, logicalW, logicalH, k, maxK;
    SDL_RendererInfo info;
    SDL_FRect dst, gameDst;
    SDL_Rect src, maskRect;

    sHaveCorrection = 0;  // (until the masks are there: none without them)
    if (PortLcdFilter() == PORT_LCD_OFF || sBroken)
        return 0;
    if (SDL_GetRendererOutputSize(renderer, &outW, &outH) != 0 || outW <= 0 || outH <= 0)
        return 0;
    if (scale < 1.5f)
        return 0;  // no room for cells (and nothing to smooth)

    k = (int)ceilf(scale - 0.01f);
    maxK = 16;
    if (SDL_GetRendererInfo(renderer, &info) == 0 && info.max_texture_width > 0)
    {
        while (maxK > 1 && (w * maxK > info.max_texture_width || h * maxK > info.max_texture_height))
            maxK--;
    }
    if (k > maxK)
        k = maxK;
    if (!MakeBig(renderer, w, h, k))
    {
        fprintf(stderr, "JVPPBPort: LCD filter not available: %s\n", SDL_GetError());
        sBroken = 1;
        return 0;
    }
    if (outW != sMaskW || outH != sMaskH || scale != sMaskScale || x0 != sMaskX || y0 != sMaskY
        || PortLcdFilter() != sMaskMode || !sCols || !sRows)
    {
        MakeMasks(renderer, outW, outH, x0, y0, scale);
        sMaskMode = PortLcdFilter();
    }
    sHaveCorrection = 1;

    // The window's pixels from here on (the logical size is put back after).
    SDL_RenderGetLogicalSize(renderer, &logicalW, &logicalH);
    SDL_RenderSetLogicalSize(renderer, 0, 0);

    SDL_SetRenderTarget(renderer, sBig);
    SDL_SetTextureBlendMode(picture, SDL_BLENDMODE_NONE);
    SDL_RenderCopy(renderer, picture, NULL, NULL);
    SDL_SetRenderTarget(renderer, NULL);

    SDL_SetRenderDrawColor(renderer, (gPortWindowBarColor >> 16) & 0xFF, (gPortWindowBarColor >> 8) & 0xFF,
                           gPortWindowBarColor & 0xFF, 255);
    SDL_RenderClear(renderer);
    dst = (SDL_FRect){x0, y0, w * scale, h * scale};
    SDL_SetTextureBlendMode(sBig, SDL_BLENDMODE_NONE);
    SDL_SetTextureAlphaMod(sBig, 255);
    SDL_RenderCopyF(renderer, sBig, NULL, &dst);

    // The cells, over the game's canvas only (whole window pixels: the
    // masks already hold the partly covered edges).
    maskRect.x = (int)floorf(x0 + gx * scale);
    maskRect.y = (int)floorf(y0 + gy * scale);
    maskRect.w = (int)ceilf(x0 + (gx + gw) * scale) - maskRect.x;
    maskRect.h = (int)ceilf(y0 + (gy + gh) * scale) - maskRect.y;
    if (maskRect.x < 0) maskRect.w += maskRect.x, maskRect.x = 0;
    if (maskRect.y < 0) maskRect.h += maskRect.y, maskRect.y = 0;
    if (maskRect.x + maskRect.w > outW) maskRect.w = outW - maskRect.x;
    if (maskRect.y + maskRect.h > outH) maskRect.h = outH - maskRect.y;
    if (maskRect.w > 0 && maskRect.h > 0)
    {
        src = (SDL_Rect){maskRect.x, 0, maskRect.w, 1};
        SDL_RenderCopy(renderer, sCols, &src, &maskRect);
        src = (SDL_Rect){0, maskRect.y, 1, maskRect.h};
        SDL_RenderCopy(renderer, sRows, &src, &maskRect);
        if (sAdd > 0)
        {
            src = (SDL_Rect){gx * sK, gy * sK, gw * sK, gh * sK};
            gameDst = (SDL_FRect){x0 + gx * scale, y0 + gy * scale, gw * scale, gh * scale};
            SDL_SetTextureBlendMode(sBig, SDL_BLENDMODE_ADD);
            SDL_SetTextureAlphaMod(sBig, Byte(sAdd));
            SDL_RenderCopyF(renderer, sBig, &src, &gameDst);
        }
    }

    SDL_RenderSetLogicalSize(renderer, logicalW, logicalH);
    return 1;
}

// Saves the window's pixels as it is now (before it's shown), for test shots.
void PortLcdSaveWindow(SDL_Renderer *renderer, const char *path)
{
    int w, h, logicalW, logicalH;
    SDL_Surface *surface;

    if (SDL_GetRendererOutputSize(renderer, &w, &h) != 0)
        return;
    // (SDL reads from the viewport's corner: the whole window, please.)
    SDL_RenderGetLogicalSize(renderer, &logicalW, &logicalH);
    SDL_RenderSetLogicalSize(renderer, 0, 0);
    surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (surface && SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch) == 0)
        SDL_SaveBMP(surface, path);
    SDL_FreeSurface(surface);
    SDL_RenderSetLogicalSize(renderer, logicalW, logicalH);
}
