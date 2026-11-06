// JVPPBPort: software renderer for the GBA picture, one scanline at a time.
// Covers what this game uses: display mode 0 (four tiled backgrounds),
// regular and rotated/scaled sprites, priorities, alpha blending and
// brightness fades. Not covered (unused by the game): bitmap modes, affine
// backgrounds, windows, mosaic.
//
// Tall view: the picture is a canvas taller than the GBA screen.
// During a Ruby/Sapphire game the board's three layers (BG1-BG3) are drawn
// for the whole table at once, with the table's sprites at their places, and
// the GBA's 240x160 screen (BG0: score row, banners, panels; and the sprites
// fixed to the screen) is drawn over it at the spot PortPopupWindow picks:
// the middle of the table, or the game's camera during scenes it frames on
// the board. Short screens with a layout (layouts.c) are rearranged piece by
// piece; any other screen is drawn centered.
// JVPPBPORT_GBAVIEW=1 gives the original 240x160 picture instead.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "main.h"
#include "constants/fields.h"
#include "jvppbport.h"

#define IO16(off) (*(u16 *)&gPortIo[off])

enum { LAYER_BG0, LAYER_BG1, LAYER_BG2, LAYER_BG3, LAYER_OBJ, LAYER_BD };

// One drawn pixel of a layer: a BGR555 color, or transparent.
struct Pixel
{
    u16 color;
    u8 opaque;
    u8 priority;
    u8 semiTransparent;  // sprites only
    u16 index;           // sprites only: OAM index (lower is in front)
};

static const u8 sObjSize[3][4][2] = {
    {{8, 8}, {16, 16}, {32, 32}, {64, 64}},   // square
    {{16, 8}, {32, 8}, {32, 16}, {64, 32}},   // wide
    {{8, 16}, {8, 32}, {16, 32}, {32, 64}},   // tall
};

static inline u16 BgPaletteColor(int index)
{
    return ((u16 *)gPortPltt)[index];
}

static inline u16 ObjPaletteColor(int index)
{
    return ((u16 *)gPortPltt)[0x100 + index];
}

// Main boards: board tiles come from staticview.c (PortStreamedTile), which
// has all 56 rows, not just the 22 in VRAM.
#define STREAMED_SLOTS 22

// Draws `count` pixels of one line of a tiled background, starting at map
// position (hofs, line + vofs). `streamed`: take char-block-2 board tiles from
// all of the board's rows instead of VRAM (see PortStreamedTile).
static void RenderTextBg(int bg, int line, int hofs, int vofs, int count, int streamed, struct Pixel *out)
{
    u16 cnt = IO16(REG_OFFSET_BG0CNT + bg * 2);
    int priority = cnt & 3;
    int charBlock = (cnt >> 2) & 3;
    const u8 *chars = &gPortVram[charBlock * 0x4000];
    int is8bpp = (cnt >> 7) & 1;
    int screenBase = ((cnt >> 8) & 0x1F) * 0x800;
    int size = (cnt >> 14) & 3;
    int width = (size & 1) ? 512 : 256;
    int height = (size & 2) ? 512 : 256;
    int y = (line + vofs) & (height - 1);
    int x;

    streamed = streamed && charBlock == 2 && !is8bpp;
    for (x = 0; x < count; x++)
    {
        int bx = (x + hofs) & (width - 1);
        int block = (bx >> 8) + ((y >> 8) * (width >> 8));
        const u16 *map = (const u16 *)&gPortVram[(screenBase + block * 0x800) & 0xFFFF];
        u16 entry = map[((y & 255) >> 3) * 32 + ((bx & 255) >> 3)];
        int tile = entry & 0x3FF;
        int px = bx & 7, py = y & 7;
        int colorIndex;

        if (entry & 0x400)
            px = 7 - px;
        if (entry & 0x800)
            py = 7 - py;

        if (is8bpp)
        {
            u32 addr = tile * 64 + py * 8 + px;
            colorIndex = chars[addr & 0xFFFF] ;
            if (colorIndex == 0)
                continue;
        }
        else
        {
            const u8 *board = streamed && tile < STREAMED_SLOTS * 32 ? PortStreamedTile(tile, y >> 3) : NULL;
            u32 addr = tile * 32 + py * 4 + (px >> 1);
            u8 pair = board ? board[py * 4 + (px >> 1)] : chars[addr & 0xFFFF];
            colorIndex = (px & 1) ? (pair >> 4) : (pair & 0xF);
            if (colorIndex == 0)
                continue;
            colorIndex += (entry >> 12) * 16;
        }
        out[x].color = BgPaletteColor(colorIndex);
        out[x].opaque = TRUE;
        out[x].priority = priority;
    }
}

// The sprite list being drawn: the hardware OAM, or in the static table view
// the game's full list (gPortSprites), one entry per OAM index.
static struct PortSprite sSprites[PORT_MAX_SPRITES];
static int sSpriteCount;

static void LoadHardwareSprites(void)
{
    const u16 *oam = (const u16 *)gPortOam;
    int i;

    for (i = 0; i < 128; i++)
    {
        memcpy(sSprites[i].attr, &oam[i * 4], 8);
        sSprites[i].kind = PORT_SPRITE_SCREEN;
        sSprites[i].ownPalette = FALSE;
    }
    sSpriteCount = 128;
}

// Draws line `line` of the sprites of one kind into `out` (`width` pixels).
// PORT_SPRITE_SCREEN: GBA screen line, positions from the OAM bits like the
// GBA. PORT_SPRITE_TABLE: table row, positions from the sprite's x/y.
static int sOnlySprite = -1;  // >= 0: RenderSprites draws only this OAM index

// Screen sprites drawn across the whole table (PortRenderScanline): out[0]
// is screen x -sScreenLeft, and only sprites that show at least partly on the
// GBA's own 240 columns are drawn (the game parks some just off its edges).
static int sScreenLeft, sScreenWide;

static void RenderSprites(int kind, int line, int width, struct Pixel *out, int opaqueOnly)
{
    int oneDimensional = (IO16(REG_OFFSET_DISPCNT) & DISPCNT_OBJ_1D_MAP) != 0;
    int i;

    // Lower OAM index wins ties, so draw from the back and let later (lower
    // index) sprites overwrite when their priority is equal or better.
    for (i = sSpriteCount - 1; i >= 0; i--)
    {
        const struct PortSprite *sprite = &sSprites[i];
        u16 attr0 = sprite->attr[0], attr1 = sprite->attr[1], attr2 = sprite->attr[2];
        int affine = (attr0 >> 8) & 1;
        int doubleSize = (attr0 >> 9) & 1;
        int mode = (attr0 >> 10) & 3;
        int is8bpp = (attr0 >> 13) & 1;
        int shape = (attr0 >> 14) & 3;
        int sizeIndex = (attr1 >> 14) & 3;
        int w, h, boundsW, boundsH, sy, sx, x;
        int tile = attr2 & 0x3FF, priority = (attr2 >> 10) & 3, pal = attr2 >> 12;
        s16 pa = 0x100, pb = 0, pc = 0, pd = 0x100;

        if (sprite->kind != kind || (sOnlySprite >= 0 && i != sOnlySprite))
            continue;
        if (!affine && doubleSize)
            continue;  // hidden
        if (shape == 3 || mode == 2 || mode == 3)
            continue;  // invalid shape / object window (unused)
        if (opaqueOnly && mode == 1)
            continue;

        w = sObjSize[shape][sizeIndex][0];
        h = sObjSize[shape][sizeIndex][1];
        boundsW = w << (affine && doubleSize);
        boundsH = h << (affine && doubleSize);

        if (kind == PORT_SPRITE_TABLE)
        {
            sy = sprite->y;
            sx = sprite->x;
        }
        else
        {
            sy = attr0 & 0xFF;
            if (sy + boundsH > 256)
                sy -= 256;  // wraps from the bottom of the 256-line space
            sx = attr1 & 0x1FF;
            if (sx >= 256)
                sx -= 512;
            if (sScreenWide)
            {
                if (sx + boundsW <= 0 || sx >= PORT_SCREEN_WIDTH)
                    continue;
                sx += sScreenLeft;
            }
        }
        if (line < sy || line >= sy + boundsH)
            continue;

        if (affine)
        {
            int m = (attr1 >> 9) & 0x1F;
            pa = sSprites[m * 4].attr[3];
            pb = sSprites[m * 4 + 1].attr[3];
            pc = sSprites[m * 4 + 2].attr[3];
            pd = sSprites[m * 4 + 3].attr[3];
        }

        for (x = 0; x < boundsW; x++)
        {
            int screenX = sx + x;
            int tx, ty, colorIndex;
            const u8 *tileData;
            int tileOffset;

            if (screenX < 0 || screenX >= width)
                continue;

            if (affine)
            {
                // Rotate/scale about the sprite's center.
                int cx = x - boundsW / 2, cy = (line - sy) - boundsH / 2;
                tx = ((pa * cx + pb * cy) >> 8) + w / 2;
                ty = ((pc * cx + pd * cy) >> 8) + h / 2;
                if (tx < 0 || tx >= w || ty < 0 || ty >= h)
                    continue;
            }
            else
            {
                tx = x;
                ty = line - sy;
                if (attr1 & 0x1000)
                    tx = w - 1 - tx;
                if (attr1 & 0x2000)
                    ty = h - 1 - ty;
            }

            if (oneDimensional)
                tileOffset = (ty >> 3) * (w >> 3) * (is8bpp ? 2 : 1);
            else
                tileOffset = (ty >> 3) * 32;
            tileOffset += (tx >> 3) * (is8bpp ? 2 : 1);
            tileData = &gPortVram[0x10000 + (((tile + tileOffset) & 0x3FF) * 32)];

            if (is8bpp)
            {
                colorIndex = tileData[(ty & 7) * 8 + (tx & 7)];
                if (colorIndex == 0)
                    continue;
            }
            else
            {
                u8 pair = tileData[(ty & 7) * 4 + ((tx & 7) >> 1)];
                colorIndex = (tx & 1) ? (pair >> 4) : (pair & 0xF);
                if (colorIndex == 0)
                    continue;
                colorIndex += pal * 16;
            }

            if (out[screenX].opaque && out[screenX].priority < priority)
                continue;
            out[screenX].color = sprite->ownPalette ? gPortSpritePalettes[i][colorIndex & 15] : ObjPaletteColor(colorIndex);
            out[screenX].opaque = TRUE;
            out[screenX].priority = priority;
            out[screenX].semiTransparent = (mode == 1);
            out[screenX].index = i;
        }
    }
}

static inline u16 Blend(u16 a, u16 b, int eva, int evb)
{
    int r = ((a & 31) * eva + (b & 31) * evb) >> 4;
    int g = (((a >> 5) & 31) * eva + ((b >> 5) & 31) * evb) >> 4;
    int bl = (((a >> 10) & 31) * eva + ((b >> 10) & 31) * evb) >> 4;

    if (r > 31) r = 31;
    if (g > 31) g = 31;
    if (bl > 31) bl = 31;
    return r | (g << 5) | (bl << 10);
}

static inline u16 Brighten(u16 c, int evy)
{
    int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;

    r += ((31 - r) * evy) >> 4;
    g += ((31 - g) * evy) >> 4;
    b += ((31 - b) * evy) >> 4;
    return r | (g << 5) | (b << 10);
}

static inline u16 Darken(u16 c, int evy)
{
    int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;

    r -= (r * evy) >> 4;
    g -= (g * evy) >> 4;
    b -= (b * evy) >> 4;
    return r | (g << 5) | (b << 10);
}

static inline u32 ToArgb(u16 c)
{
    u32 r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;

    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

// The registers that decide how layers are combined.
struct BlendRegs
{
    u16 dispcnt, bldcnt;
    int effect, eva, evb, evy;
    u16 backdrop;
};

static void ReadBlendRegs(struct BlendRegs *r)
{
    u16 bldalpha = IO16(REG_OFFSET_BLDALPHA);

    r->dispcnt = IO16(REG_OFFSET_DISPCNT);
    r->bldcnt = IO16(REG_OFFSET_BLDCNT);
    r->effect = (r->bldcnt >> 6) & 3;
    r->eva = bldalpha & 0x1F;
    r->evb = (bldalpha >> 8) & 0x1F;
    r->evy = IO16(REG_OFFSET_BLDY) & 0x1F;
    r->backdrop = BgPaletteColor(0);
    if (r->eva > 16) r->eva = 16;
    if (r->evb > 16) r->evb = 16;
    if (r->evy > 16) r->evy = 16;
}

// Combines one pixel of each layer (BG0-BG3, sprites) into the final color.
static u32 Composite(const struct Pixel *px[5], const struct BlendRegs *r)
{
    static const int order[5] = {LAYER_OBJ, LAYER_BG0, LAYER_BG1, LAYER_BG2, LAYER_BG3};
    int top = LAYER_BD, second = LAYER_BD;
    u16 topColor = r->backdrop, secondColor = r->backdrop;
    u16 color;
    int p, k;

    // Find the two front-most layers: lower priority number is in front;
    // sprites beat backgrounds of equal priority; BG0 beats BG1 and so on.
    for (p = 0; p < 4 && second == LAYER_BD; p++)
    {
        for (k = 0; k < 5; k++)
        {
            const struct Pixel *pix = px[order[k]];
            if (!pix->opaque || pix->priority != p)
                continue;
            if (top == LAYER_BD)
            {
                top = order[k];
                topColor = pix->color;
            }
            else
            {
                second = order[k];
                secondColor = pix->color;
                break;
            }
        }
    }

    color = topColor;
    if (top == LAYER_OBJ && px[LAYER_OBJ]->semiTransparent
        && (r->bldcnt & (0x100 << second)))
    {
        color = Blend(topColor, secondColor, r->eva, r->evb);
    }
    else if (r->bldcnt & (1 << top))
    {
        if (r->effect == 1 && (r->bldcnt & (0x100 << second)))
            color = Blend(topColor, secondColor, r->eva, r->evb);
        else if (r->effect == 2)
            color = Brighten(topColor, r->evy);
        else if (r->effect == 3)
            color = Darken(topColor, r->evy);
    }
    return ToArgb(color);
}

static const struct Pixel sClear;

// Draws one line of the GBA screen (all layers, current registers).
static void RenderGbaLine(int line, uint32_t *out)
{
    static struct Pixel layers[5][PORT_SCREEN_WIDTH];
    struct BlendRegs regs;
    const struct Pixel *px[5];
    int bg, x;

    ReadBlendRegs(&regs);
    if (regs.dispcnt & DISPCNT_FORCED_BLANK)
    {
        for (x = 0; x < PORT_SCREEN_WIDTH; x++)
            out[x] = 0xFFFFFFFF;
        return;
    }

    memset(layers, 0, sizeof(layers));
    for (bg = 0; bg < 4; bg++)
    {
        if (regs.dispcnt & (DISPCNT_BG0_ON << bg))
            RenderTextBg(bg, line, IO16(REG_OFFSET_BG0HOFS + bg * 4) & 0x1FF,
                         IO16(REG_OFFSET_BG0VOFS + bg * 4) & 0x1FF, PORT_SCREEN_WIDTH, FALSE, layers[bg]);
    }
    if (regs.dispcnt & DISPCNT_OBJ_ON)
        RenderSprites(PORT_SPRITE_SCREEN, line, PORT_SCREEN_WIDTH, layers[LAYER_OBJ], FALSE);

    for (x = 0; x < PORT_SCREEN_WIDTH; x++)
    {
        for (bg = 0; bg < 5; bg++)
            px[bg] = &layers[bg][x];
        out[x] = Composite(px, &regs);
    }
}

// Draws lines y..y+h-1, columns 0..w-1 of the backgrounds as they are now
// (no sprites, no blending or fades) into `out` (w pixels per line). For
// layouts that show more than the game draws at once (see pokedex.c).
void PortCaptureBgLines(int y, int h, int w, uint32_t *out)
{
    PortCaptureBgLayers(y, h, w, out, 0xF);
}

// One background's lines 0..h-1 from map x `mapX` (its own vertical scroll),
// ignoring the game's horizontal scroll; alpha 0 where it has no pixel.
void PortCaptureBgMap(int bg, int mapX, int w, int h, uint32_t *out)
{
    static struct Pixel line[PORT_SCREEN_WIDTH];
    struct BlendRegs regs;
    const struct Pixel *px[5];
    int y, x, i;

    ReadBlendRegs(&regs);
    regs.bldcnt = 0;
    regs.effect = 0;
    for (i = 0; i < 5; i++)
        px[i] = &sClear;
    for (y = 0; y < h; y++)
    {
        memset(line, 0, sizeof(line));
        if (regs.dispcnt & (DISPCNT_BG0_ON << bg))
            RenderTextBg(bg, y, mapX & 0x1FF, IO16(REG_OFFSET_BG0VOFS + bg * 4) & 0x1FF, w, FALSE, line);
        for (x = 0; x < w; x++)
        {
            px[bg] = &line[x];
            out[y * w + x] = line[x].opaque ? Composite(px, &regs) : 0;
        }
    }
}

// The same with only some backgrounds (bit n: BGn); where none of them has a
// pixel, the picture is see-through (alpha 0: layouts draw what's under it).
void PortCaptureBgLayers(int y, int h, int w, uint32_t *out, int mask)
{
    static struct Pixel layers[4][PORT_SCREEN_WIDTH];
    struct BlendRegs regs;
    const struct Pixel *px[5];
    int line, bg, x;

    ReadBlendRegs(&regs);
    regs.bldcnt = 0;
    regs.effect = 0;
    px[LAYER_OBJ] = &sClear;
    for (line = y; line < y + h; line++)
    {
        memset(layers, 0, sizeof(layers));
        for (bg = 0; bg < 4; bg++)
        {
            if ((regs.dispcnt & (DISPCNT_BG0_ON << bg)) && (mask & (1 << bg)))
                RenderTextBg(bg, line, IO16(REG_OFFSET_BG0HOFS + bg * 4) & 0x1FF,
                             IO16(REG_OFFSET_BG0VOFS + bg * 4) & 0x1FF, w, FALSE, layers[bg]);
        }
        for (x = 0; x < w; x++)
        {
            int any = FALSE;
            for (bg = 0; bg < 4; bg++)
            {
                px[bg] = &layers[bg][x];
                any |= layers[bg][x].opaque;
            }
            out[(line - y) * w + x] = any ? Composite(px, &regs) : 0;
        }
    }
}

// ---- The tall canvas ----

static uint32_t *sCanvas;
static int sTableMode;           // drawing a whole main board this frame
static int sWindowX, sWindowY;   // canvas position of the GBA screen's top-left
static int sBoardX, sBoardY;     // board position where the GBA screen is drawn (PortPopupWindow)
// The pause panel drawn apart from the GBA screen (PortPauseSprite): its
// board position, and its finished pixels per GBA line (alpha 0: none).
static int sPauseX, sPauseY, sPauseSplit;
static uint32_t sPauseLines[PORT_SCREEN_HEIGHT][PORT_TABLE_WIDTH];
static void DrawPausePanel(void);
static int sPopupAtCamera;       // ... and that is where the game's camera is
// The board layers BG1-BG3 and the table's sprites for the whole table,
// drawn once per frame.
static struct Pixel sBoard[3][PORT_TABLE_HEIGHT][PORT_TABLE_WIDTH];
static struct Pixel sTableSprites[PORT_TABLE_HEIGHT][PORT_TABLE_WIDTH];
// The HUD's pixels (see TakeHudPixels), by GBA screen line.
static struct Pixel sHud[PORT_SCREEN_HEIGHT][PORT_SCREEN_WIDTH];
static struct BlendRegs sHudRegs[PORT_SCREEN_HEIGHT];
static s16 sHudRightFrom[PORT_SCREEN_HEIGHT];  // screen x where the score row's right half starts, or -1
static s16 sHudBoltFrom[PORT_SCREEN_HEIGHT];   // screen x of the bolt's column on the score row
static s16 sHudTimerFrom[PORT_SCREEN_HEIGHT];  // screen x where the timer starts on this line, or -1
static s8 sHudTimerRow[PORT_SCREEN_HEIGHT];    // the timer's pixel row (0-15) on this line

// This frame's canvas size (see PortRenderBegin), and the menu size.
static int sFrameW = PORT_CANVAS_WIDTH, sFrameH = PORT_CANVAS_HEIGHT;
static int sMenuW = PORT_CANVAS_WIDTH, sMenuH = PORT_CANVAS_HEIGHT;

void PortSetMenuSize(int w, int h)
{
    sMenuW = w < PORT_CANVAS_WIDTH ? PORT_CANVAS_WIDTH : w > PORT_MENU_MAX_W ? PORT_MENU_MAX_W : w;
    sMenuH = h < PORT_CANVAS_HEIGHT ? PORT_CANVAS_HEIGHT : h > PORT_MENU_MAX_H ? PORT_MENU_MAX_H : h;
}

void PortMenuSize(int *w, int *h)
{
    *w = sMenuW;
    *h = sMenuH;
}

int PortCanvasWidth(void)
{
    return gPortStaticView ? sFrameW : PORT_SCREEN_WIDTH;
}

int PortCanvasHeight(void)
{
    return gPortStaticView ? sFrameH : PORT_SCREEN_HEIGHT;
}

// A Ruby/Sapphire board is on screen (set up in all_board_pinball_game_main.c:
// BG3 is the 256x512 board background on char block 2, screen block 0).
static int MainBoardShown(void)
{
    u16 bg3cnt = IO16(REG_OFFSET_BG3CNT);

    if (gMain.mainState != STATE_GAME_MAIN && gMain.mainState != STATE_GAME_IDLE)
        return FALSE;
    if (gMain.selectedField >= FIELD_BONUS_START)
        return FALSE;
    return (bg3cnt & (BGCNT_TXT256x512 | (3 << 2) | (0x1F << 8))) == (BGCNT_TXT256x512 | BGCNT_CHARBASE(2) | BGCNT_SCREENBASE(0));
}

static void KeepTable(const struct BlendRegs *regs);

// ---- Short screens rearranged by a layout ----

static const struct PortLayout *sLayout;  // this frame's layout, or NULL
#define FILL_MAX 64
static struct Pixel sFill[FILL_MAX][FILL_MAX];  // the fill pattern

// The fill pattern's pixel under canvas position (x, y), or a clear one.
static const struct Pixel *FillPixel(int x, int y)
{
    if (sLayout->fillBg < 0)
        return &sClear;
    return &sFill[y % sLayout->fillH][x % sLayout->fillW];
}

// Whole-screen fades (every layer shown is a fade target) apply to pictures
// too.
static u32 FadeArgb(u32 c, const struct BlendRegs *regs)
{
    int k, evy = regs->evy;
    u32 out = 0xFF000000;

    if ((regs->bldcnt & 0x1F) != ((regs->dispcnt >> 8) & 0x1F) || (regs->effect != 2 && regs->effect != 3) || evy == 0)
        return c;
    for (k = 0; k < 24; k += 8)
    {
        int v = (c >> k) & 0xFF;
        v = regs->effect == 2 ? v + (((255 - v) * evy) >> 4) : v - ((v * evy) >> 4);
        out |= (u32)v << k;
    }
    return out;
}

static u32 Scale(u32 c, int brightness)
{
    u32 out = 0xFF000000;
    int k;

    for (k = 0; k < 24; k += 8)
    {
        u32 v = (((c >> k) & 0xFF) * brightness) >> 8;
        out |= (v > 255 ? 255 : v) << k;
    }
    return out;
}

// Averages the source pixels under one pixel of a picture drawn at another
// size.
static u32 ScaledPixel(const struct PortLayoutImage *im, int x, int y, int dw, int dh)
{
    int x0 = x * im->w / dw, x1 = (x + 1) * im->w / dw;
    int y0 = y * im->h / dh, y1 = (y + 1) * im->h / dh;
    u32 r = 0, g = 0, b = 0, n = 0;
    int sx, sy;

    if (x1 <= x0) x1 = x0 + 1;
    if (y1 <= y0) y1 = y0 + 1;
    for (sy = y0; sy < y1; sy++)
    {
        for (sx = x0; sx < x1; sx++)
        {
            u32 q = im->pixels[sy * im->w + sx];
            r += (q >> 16) & 0xFF;
            g += (q >> 8) & 0xFF;
            b += q & 0xFF;
            n++;
        }
    }
    return 0xFF000000 | ((r / n) << 16) | ((g / n) << 8) | (b / n);
}

static void DrawLayoutImage(uint32_t *canvas, const struct PortLayoutImage *im, const struct BlendRegs *regs)
{
    int w = im->dw ? im->dw : im->w, h = im->dh ? im->dh : im->h;
    int x, y;

    for (y = 0; y < h; y++)
    {
        int cy = im->dy + y;
        if (cy < 0 || cy >= PortCanvasHeight())
            continue;
        for (x = 0; x < w; x++)
        {
            int cx = im->dx + x;
            u32 c;
            if (cx < 0 || cx >= PortCanvasWidth())
                continue;
            if (!im->pixels)
                c = im->color;
            else if (w != im->w || h != im->h)
                c = ScaledPixel(im, x, y, w, h);
            else
                c = im->pixels[y * im->w + x];
            if (!(c >> 24))
                continue;  // see-through (PortCaptureBgLayers)
            if (im->brightness != 256)
                c = Scale(c, im->brightness);
            if ((c >> 24) != 0xFF)
            {
                // Half see-through (e.g. a shadow): mixed with what's there.
                u32 a = c >> 24, under = canvas[cy * PortCanvasWidth() + cx], over = FadeArgb(c | 0xFF000000, regs), mix = 0xFF000000;
                int k;

                for (k = 0; k < 24; k += 8)
                    mix |= ((((over >> k) & 0xFF) * a + ((under >> k) & 0xFF) * (255 - a)) / 255) << k;
                canvas[cy * PortCanvasWidth() + cx] = mix;
                continue;
            }
            canvas[cy * PortCanvasWidth() + cx] = FadeArgb(c, regs);
        }
    }
}

static void DrawLayoutTile(uint32_t *canvas, const struct PortLayoutTile *t, const struct BlendRegs *regs)
{
    static struct Pixel patch[FILL_MAX][FILL_MAX];
    const struct Pixel *px[5];
    int x, y, bg;

    if (!(regs->dispcnt & (DISPCNT_BG0_ON << t->bg)))
        return;
    memset(patch, 0, sizeof(patch));
    for (y = 0; y < t->h; y++)
        RenderTextBg(t->bg, y, t->mapX, t->mapY, t->w, FALSE, patch[y]);
    for (bg = 0; bg < 5; bg++)
        px[bg] = &sClear;
    for (y = 0; y < t->dh; y++)
    {
        int cy = t->dy + y;
        if (cy < 0 || cy >= PortCanvasHeight())
            continue;
        for (x = 0; x < t->dw; x++)
        {
            int cx = t->dx + x;
            if (cx < 0 || cx >= PortCanvasWidth())
                continue;
            px[t->bg] = &patch[y % t->h][x % t->w];
            canvas[cy * PortCanvasWidth() + cx] = Composite(px, regs);
        }
    }
}

static void LayoutBegin(uint32_t *canvas)
{
    int width = PortCanvasWidth(), height = PortCanvasHeight();
    struct BlendRegs regs;
    const struct Pixel *px[5];
    int x, y, bg;

    ReadBlendRegs(&regs);
    if (regs.dispcnt & DISPCNT_FORCED_BLANK)
    {
        for (x = 0; x < width * height; x++)
            canvas[x] = 0xFFFFFFFF;
        return;
    }
    memset(sFill, 0, sizeof(sFill));
    if (sLayout->fillBg >= 0 && (regs.dispcnt & (DISPCNT_BG0_ON << sLayout->fillBg)))
    {
        int bg = sLayout->fillBg;
        for (y = 0; y < sLayout->fillH; y++)
            RenderTextBg(bg, sLayout->fillY + y, (IO16(REG_OFFSET_BG0HOFS + bg * 4) + sLayout->fillX) & 0x1FF,
                         IO16(REG_OFFSET_BG0VOFS + bg * 4) & 0x1FF, sLayout->fillW, FALSE, sFill[y]);
    }
    px[LAYER_OBJ] = &sClear;
    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            if (sLayout->image)
            {
                canvas[y * width + x] = FadeArgb(sLayout->image[y * PORT_CANVAS_WIDTH + x], &regs);
                continue;
            }
            for (bg = 0; bg < 4; bg++)
                px[bg] = bg == sLayout->fillBg ? FillPixel(x, y) : &sClear;
            canvas[y * width + x] = Composite(px, &regs);
        }
    }
    for (x = 0; x < sLayout->tileCount; x++)
        DrawLayoutTile(canvas, &sLayout->tiles[x], &regs);
    for (x = 0; x < sLayout->imageCount; x++)
        DrawLayoutImage(canvas, &sLayout->images[x], &regs);
}

// Draws the parts of GBA screen line `line` that the layout places somewhere.
static void LayoutScanline(int line)
{
    static struct Pixel layers[5][PORT_SCREEN_WIDTH];
    static struct Pixel opaqueObj[PORT_SCREEN_WIDTH];
    static struct Pixel own[4][PORT_SCREEN_WIDTH];
    int width = PortCanvasWidth(), height = PortCanvasHeight();
    struct BlendRegs regs;
    const struct Pixel *px[5];
    int i, bg, x;

    if (line >= PORT_SCREEN_HEIGHT)
    {
        for (i = 0; i < sLayout->pieceCount; i++)
        {
            if (sLayout->pieces[i].sy + sLayout->pieces[i].h > line && sLayout->pieces[i].sy <= line)
                break;
        }
        if (i == sLayout->pieceCount)
            return;
    }

    ReadBlendRegs(&regs);
    if (regs.dispcnt & DISPCNT_FORCED_BLANK)
        return;
    memset(layers, 0, sizeof(layers));
    for (bg = 0; bg < 4; bg++)
    {
        if (regs.dispcnt & (DISPCNT_BG0_ON << bg))
            RenderTextBg(bg, line, IO16(REG_OFFSET_BG0HOFS + bg * 4) & 0x1FF,
                         IO16(REG_OFFSET_BG0VOFS + bg * 4) & 0x1FF, PORT_SCREEN_WIDTH, FALSE, layers[bg]);
    }
    memset(opaqueObj, 0, sizeof(opaqueObj));
    if (regs.dispcnt & DISPCNT_OBJ_ON)
    {
        RenderSprites(PORT_SPRITE_SCREEN, line, PORT_SCREEN_WIDTH, layers[LAYER_OBJ], FALSE);
        RenderSprites(PORT_SPRITE_SCREEN, line, PORT_SCREEN_WIDTH, opaqueObj, TRUE);
    }

    for (i = 0; i < sLayout->pieceCount; i++)
    {
        const struct PortLayoutPiece *pc = &sLayout->pieces[i];
        int r, cy;

        if (line < pc->sy || line >= pc->sy + pc->h)
            continue;
        // Every canvas row showing this line (more than one if stretched, dh).
        for (r = line - pc->sy; r < (pc->dh ? pc->dh : pc->h); r += pc->h)
        {
            cy = pc->dy + r;
            if (cy < 0 || cy >= height)
                continue;
            if (pc->bgX)
            {
                // The piece's own scroll: its backgrounds drawn again, from map
                // x (bgX - 1) at the rectangle's left edge.
                for (bg = 0; bg < 4; bg++)
                {
                    memset(own[bg], 0, sizeof(own[bg]));
                    if ((pc->layers & (1 << bg)) && (regs.dispcnt & (DISPCNT_BG0_ON << bg)))
                        RenderTextBg(bg, line, (pc->bgX - 1 - pc->sx) & 0x1FF,
                                     IO16(REG_OFFSET_BG0VOFS + bg * 4) & 0x1FF, PORT_SCREEN_WIDTH, FALSE, own[bg]);
                }
            }
            for (x = 0; x < pc->w; x++)
            {
                int sx = pc->sx + x, cx = pc->dx + x;
                int any = FALSE;

                if (sx < 0 || sx >= PORT_SCREEN_WIDTH || cx < 0 || cx >= width)
                    continue;
                for (bg = 0; bg < 5; bg++)
                {
                    int layer = bg == 4 ? LAYER_OBJ : bg;
                    if (pc->layers & (1 << bg))
                    {
                        if (layer == LAYER_OBJ)
                            px[layer] = pc->opaqueObjOnly ? &opaqueObj[sx] : &layers[layer][sx];
                        else
                            px[layer] = pc->bgX ? &own[layer][sx] : &layers[layer][sx];
                        any |= px[layer]->opaque;
                    }
                    else
                    {
                        px[layer] = pc->fillUnder && bg == sLayout->fillBg ? FillPixel(cx, cy) : &sClear;
                    }
                }
                if (!any && pc->fillUnder)
                    continue;  // nothing of this piece here: keep what's under it
                if (pc->darken)
                {
                    struct BlendRegs dark = regs;
                    dark.bldcnt = 0x1F | (3 << 6);
                    dark.dispcnt = 0x1F00;
                    dark.effect = 3;
                    dark.evy = pc->darken;
                    sCanvas[cy * width + cx] = FadeArgb(Composite(px, &regs), &dark);
                    continue;
                }
                sCanvas[cy * width + cx] = Composite(px, &regs);
            }
        }
    }
}


// ---- Options: the port's boxes, PINBALL KEYS, SCREEN and MENU KEYS (screenmenu.c) ----
// Drawn over the GBA screen from BUTTON CONFIG's box down (line 64) and on
// under it, made from the Options screen's own backgrounds, so they look
// like part of it: the orange frame and BUTTON CONFIG's box (its top with
// the name tab, shortened for the box's name; its plain middle; its rounded
// bottom) are repeated, and the rows are written in the game's label style
// (text.c). Lines are
// taken with matching odd/even rows, so the stripes run on. The game's hand
// cursor (put out of sight by options.c) is drawn at its row.
#define PANEL_TAB_TEXT_X 17   // strokes of the box's name (as BUTTON CONFIG's)
#define PANEL_LABEL_X 41      // as TYPE A's
#define PANEL_VALUE_X 124     // the value's middle, between its arrows
static u32 sBgLines[PORT_SCREEN_HEIGHT][PORT_SCREEN_WIDTH];
static struct BlendRegs sPanelRegs;

static int PanelRows(void)
{
    return gPortStaticView && gMain.mainState == STATE_OPTIONS ? PortOptionsRows() : 0;
}

// Lines of the panel under the GBA screen (it starts on the screen; short
// boxes get the frame drawn on to the screen's end).
static int PanelHeight(void)
{
    if (!PanelRows())
        return 0;
    return max(0, PortOptionsPanelTop() + PortOptionsPanelHeight() - PORT_SCREEN_HEIGHT);
}

static void PanelPut(int x, int y, u32 color)
{
    int cx = sWindowX + x, cy = sWindowY + y;

    if (x >= 0 && x < PORT_SCREEN_WIDTH && cx >= 0 && cx < PortCanvasWidth() && cy >= 0 && cy < PortCanvasHeight())
        sCanvas[cy * PortCanvasWidth() + cx] = FadeArgb(color, &sPanelRegs);
}

// A small black arrow like the sound test's, pointing left (dir -1) or right.
static void PanelArrow(int x, int y, int dir)
{
    int r, c;

    for (r = 0; r < 5; r++)
        for (c = 0; c < 3; c++)
            if (c >= (r < 3 ? 2 - r : r - 2))
                PanelPut(dir < 0 ? x + c : x + 2 - c, y + r, 0xFF000000);
}

// The source pixel (on the GBA screen) for panel pixel (x, y): box lines
// from BUTTON CONFIG's box, frame lines from the frame. *dim: it is part
// of a name tab that isn't shown (drawn darker).
//
// A box's top is a yellow band (its first 4 lines) with the name tab
// hanging under it, its right edge slanting in from 10 pixels past the
// name to 3. A box with two tabs (KEYS, PAD) gets the second one after the
// first's slanted edge, its left edge the same slant 2 pixels on.
#define TAB_GAP 2
static void PanelSource(int x, int y, int *sx, int *sy, int *dim)
{
    int b, first, rows, top, k;
    const char *name;

    *sx = x;
    *sy = 158 + (y & 1);  // the frame
    *dim = FALSE;
    for (b = 0; b < PortOptionsBoxCount(); b++)
    {
        PortOptionsBox(b, &name, &first, &rows, &top);
        rows += PortOptionsBoxNote(b) != NULL;  // the note takes a row's lines
        k = y - top;
        if (k < 0 || k >= 20 + 12 * rows)
            continue;
        if (k < 12)
        {
            // The box's top: the tab shortened to fit the name.
            const char *names[2];
            int textX[2], shown, tabs = PortOptionsBoxTabs(b, names, textX, &shown);
            int textEnd = textX[0] + PortBoldTextWidth(names[0]) + 1;  // the name's outline ends here
            int slant = 14 - k;  // the tab's yellow goes this far past textEnd (on its lines, k >= 4)

            *sy = 64 + k;
            if (tabs == 2)
            {
                int end2 = textX[1] + PortBoldTextWidth(names[1]) + 1, tab;

                if (k >= 4 && x >= textEnd + slant + TAB_GAP && x < end2)
                {
                    *sx = 15;  // the second tab
                    *dim = shown != 1;
                    return;
                }
                if (x >= end2 && x < end2 + 51)
                {
                    *sx = 69 + x - end2;  // its slanted edge
                    *dim = shown != 1 && k >= 4 && x < end2 + slant;
                    return;
                }
                tab = k >= 4 && x < textEnd + slant;  // the first tab's yellow
                *dim = tab && shown != 0;
                if (x >= end2 + 51)
                {
                    if (x < 200)
                        *sx = 120;
                    return;
                }
            }
            if (x >= PANEL_TAB_TEXT_X - 1 && x < textEnd)
                *sx = 15;
            else if (x >= textEnd && x < textEnd + 51)
                *sx = 69 + x - textEnd;
            else if (x >= textEnd + 51 && x < 200)
                *sx = 120;
        }
        else if (k < 16 + 12 * rows)
            *sy = 76 + (y & 1);              // the box's middle
        else
            *sy = 148 + k - (16 + 12 * rows);  // its bottom
    }
}

// Draws the panel; returns the canvas line under it (the canvas's height
// when it goes on to the bottom, on touch screens).
static int DrawOptionsPanel(void)
{
    static struct Pixel layers[4][PORT_SCREEN_WIDTH];
    static struct Pixel obj[PORT_SCREEN_WIDTH];
    const struct Pixel *px[5];
    int rows = PanelRows(), height = PanelHeight(), last = PORT_SCREEN_HEIGHT + height;
    int panelTop = PortOptionsPanelTop();
    int line, x, y, bg, r, b, handRow, shift;

    ReadBlendRegs(&sPanelRegs);
    // The GBA screen's lines, backgrounds only (no cursor or arrows).
    for (line = 0; line < PORT_SCREEN_HEIGHT; line++)
    {
        for (bg = 0; bg < 4; bg++)
        {
            memset(layers[bg], 0, sizeof(layers[bg]));
            if (sPanelRegs.dispcnt & (DISPCNT_BG0_ON << bg))
                RenderTextBg(bg, line, IO16(REG_OFFSET_BG0HOFS + bg * 4) & 0x1FF,
                             IO16(REG_OFFSET_BG0VOFS + bg * 4) & 0x1FF, PORT_SCREEN_WIDTH, FALSE, layers[bg]);
        }
        px[LAYER_OBJ] = &sClear;
        for (x = 0; x < PORT_SCREEN_WIDTH; x++)
        {
            for (bg = 0; bg < 4; bg++)
                px[bg] = &layers[bg][x];
            sBgLines[line][x] = (sPanelRegs.dispcnt & DISPCNT_FORCED_BLANK) ? 0xFFFFFFFF : Composite(px, &sPanelRegs);
        }
    }
    if (PortCanvasWidth() != PORT_CANVAS_WIDTH || PortCanvasHeight() != PORT_CANVAS_HEIGHT)
        last = PortCanvasHeight() - sWindowY;  // the frame goes on to the bottom
    for (y = panelTop; y < last; y++)
    {
        for (x = 0; x < PORT_SCREEN_WIDTH; x++)
        {
            int sx, sy, dim;

            PanelSource(x, y, &sx, &sy, &dim);
            if (sy < PORT_SCREEN_HEIGHT && sx < PORT_SCREEN_WIDTH)
            {
                u32 c = sBgLines[sy][sx];

                if (dim && (c >> 16 & 0xFF) > 200 && (c >> 8 & 0xFF) > 150 && (c & 0xFF) < 100)  // a tab not shown: darker yellow
                    c = (((c >> 16 & 0xFF) * 190 >> 8) << 16) | (((c >> 8 & 0xFF) * 170 >> 8) << 8) | ((c & 0xFF) * 170 >> 8);
                PanelPut(x, y, c | 0xFF000000);
            }
        }
    }
    // The names and the rows (the game's label colors: white in black).
    for (b = 0; b < PortOptionsBoxCount(); b++)
    {
        const char *name;
        int first, count, top;

        const char *names[2];
        int textX[2], shown, t, tabs;

        PortOptionsBox(b, &name, &first, &count, &top);
        tabs = PortOptionsBoxTabs(b, names, textX, &shown);
        for (t = 0; t < tabs; t++)
            PortDrawBoldText(textX[t], top + 4, names[t], t == shown ? 0xFFFFFFFF : 0xFFB0B0B0, 0xFF000000, PanelPut);
        if ((name = PortOptionsBoxNote(b)) != NULL)
            PortDrawBoldText(PANEL_LABEL_X, PortOptionsRowY(first + count - 1) + 12, name, 0xFFB0B0B0, 0xFF000000, PanelPut);
    }
    for (r = 0; r < rows; r++)
    {
        const char *value = PortOptionsRowValue(r);
        int ry = PortOptionsRowY(r);

        PortDrawBoldText(PANEL_LABEL_X, ry, PortOptionsRowLabel(r), 0xFFFFFFFF, 0xFF000000, PanelPut);
        if (PortOptionsRowHasArrows(r))
        {
            PanelArrow(PANEL_VALUE_X - 26, ry, -1);
            PanelArrow(PANEL_VALUE_X + 24, ry, 1);
            PortDrawBoldText(PANEL_VALUE_X - PortBoldTextWidth(value) / 2, ry, value, 0xFFFFFFFF, 0xFF000000, PanelPut);
        }
        else
        {
            PortDrawBoldText(PortOptionsRowKeysX(r), ry, value, 0xFFFFFFFF, 0xFF000000, PanelPut);
        }
    }
    // The hand cursor, when options.c put it on a row: its sprite's lines
    // at PORT_OPTIONS_HAND_Y are drawn `shift` lines lower, at the row.
    sOnlySprite = PortOptionsHandSprite(&handRow);
    shift = sOnlySprite >= 0 ? PortOptionsRowY(handRow) - 2 - PORT_OPTIONS_HAND_Y : 0;
    if (sOnlySprite >= 0 && (sPanelRegs.dispcnt & DISPCNT_OBJ_ON))
    {
        for (line = PORT_SCREEN_HEIGHT; line < 256; line++)
        {
            memset(obj, 0, sizeof(obj));
            RenderSprites(PORT_SPRITE_SCREEN, line, PORT_SCREEN_WIDTH, obj, FALSE);
            for (x = 0; x < PORT_SCREEN_WIDTH; x++)
            {
                if (!obj[x].opaque)
                    continue;
                for (bg = 0; bg < 4; bg++)
                    px[bg] = &sClear;
                px[LAYER_OBJ] = &obj[x];
                {
                    int cx = sWindowX + x, cy = sWindowY + line + shift;
                    if (cx >= 0 && cx < PortCanvasWidth() && cy >= 0 && cy < PortCanvasHeight())
                        sCanvas[cy * PortCanvasWidth() + cx] = Composite(px, &sPanelRegs);
                }
            }
        }
    }
    sOnlySprite = -1;
    return sWindowY + last;
}

// After the frame's lines: a GBA-sized screen (no layout) on a menu canvas
// bigger than 256x448 (touch screens) has its outermost pixels carried out
// to the canvas's edges, so it fills the screen (Options' orange border goes
// on to the edges, a white screen stays white), fades included. Not the
// intro (black bars).
void PortRenderEnd(void)
{
    int width = PortCanvasWidth(), height = PortCanvasHeight(), x, y;
    int x0 = sWindowX, x1 = sWindowX + PORT_SCREEN_WIDTH, y0 = sWindowY, y1 = sWindowY + PORT_SCREEN_HEIGHT;

    if (sTableMode && sPauseSplit)
        DrawPausePanel();
    if (gPortStaticView && !sTableMode && !sLayout && x0 >= 0 && y0 >= 0 && PanelRows())
        y1 = DrawOptionsPanel();
    if (!gPortStaticView || sTableMode || sLayout
        || (width == PORT_CANVAS_WIDTH && height == PORT_CANVAS_HEIGHT) || x0 < 0 || y0 < 0)
        return;
    if (gMain.mainState == STATE_INTRO)
        return;  // the intro's moving scenes would smear; black bars there
    for (y = y0; y < y1; y++)
    {
        uint32_t *row = &sCanvas[y * width];
        for (x = 0; x < x0; x++)
            row[x] = row[x0];
        for (x = x1; x < width; x++)
            row[x] = row[x1 - 1];
    }
    for (y = 0; y < y0; y++)
        memcpy(&sCanvas[y * width], &sCanvas[y0 * width], width * 4);
    for (y = y1; y < height; y++)
        memcpy(&sCanvas[y * width], &sCanvas[(y1 - 1) * width], width * 4);
}

void PortRenderBegin(uint32_t *canvas)
{
    int width, height, menu;
    struct BlendRegs regs;
    const struct Pixel *px[5];
    int i, x, y, bg;
    int wasTable, cameraX, cameraY;

    // A game keeps the table's canvas; everything else gets the menu size.
    menu = !MainBoardShown() && gMain.mainState != STATE_GAME_MAIN;
    sFrameW = menu ? sMenuW : PORT_CANVAS_WIDTH;
    sFrameH = menu ? sMenuH : PORT_CANVAS_HEIGHT;
    width = PortCanvasWidth();
    height = PortCanvasHeight();
    sCanvas = canvas;
    sLayout = NULL;
    wasTable = sTableMode;
    sTableMode = gPortStaticView && MainBoardShown();
    if (sTableMode)
    {
        memcpy(sSprites, gPortSprites, gPortSpriteCount * sizeof(sSprites[0]));
        sSpriteCount = gPortSpriteCount;
    }
    else
    {
        LoadHardwareSprites();
    }
    if (!gPortStaticView)
    {
        sWindowX = sWindowY = 0;
        return;
    }
    if (!sTableMode && (sLayout = PortScreenLayout()) != NULL)
    {
        LayoutBegin(canvas);
        return;
    }
    if (!sTableMode)
    {
        // The GBA screen centered, on black (on a bigger menu canvas its
        // edges are carried out to the canvas's edges, PortRenderEnd).
        sWindowX = (width - PORT_SCREEN_WIDTH) / 2;
        // Options: centered with its boxes (on touch screens they end
        // above the GBA screen's end; the frame goes on under them). If
        // they don't quite fit (PC: 4 lines with E-READER), the frame under
        // the last box runs off the bottom instead of the screen's top.
        sWindowY = (height - (PanelRows() ? PortOptionsPanelTop() + PortOptionsPanelHeight() : PORT_SCREEN_HEIGHT)) / 2;
        if (sWindowY < 0)
            sWindowY = 0;
        for (i = 0; i < width * height; i++)
            canvas[i] = 0xFF000000;
        return;
    }

    cameraX = IO16(REG_OFFSET_BG3HOFS) & 0x1FF;
    cameraY = IO16(REG_OFFSET_BG3VOFS) & 0x1FF;
    if (cameraX >= 256) cameraX -= 512;
    if (cameraY >= 256 + 128) cameraY -= 512;  // small negative scrolls (shake at the top)
    PortPopupWindow(cameraX, cameraY, !wasTable, &sBoardX, &sBoardY);
    sPopupAtCamera = sBoardX == cameraX && sBoardY == cameraY;
    // Paused while the GBA screen is somewhere else (a scene at the camera,
    // or sliding): the pause panel's sprites go in the middle by themselves.
    PortPauseSpot(&sPauseX, &sPauseY);
    sPauseSplit = FALSE;
    if (sPauseX != sBoardX || sPauseY != sBoardY)
    {
        for (i = 0; i < sSpriteCount; i++)
        {
            if (sSprites[i].kind == PORT_SPRITE_SCREEN && PortPauseSprite(i))
            {
                sSprites[i].kind = PORT_SPRITE_PAUSE;
                sPauseSplit = TRUE;
            }
        }
    }
    if (sPauseSplit)
        memset(sPauseLines, 0, sizeof(sPauseLines));
    sWindowX = PORT_TABLE_X + sBoardX;
    sWindowY = PORT_TABLE_Y + sBoardY;

    ReadBlendRegs(&regs);
    if (regs.dispcnt & DISPCNT_FORCED_BLANK)
    {
        for (i = 0; i < width * height; i++)
            canvas[i] = 0xFFFFFFFF;
        return;
    }

    memset(sBoard, 0, sizeof(sBoard));
    for (bg = 1; bg < 4; bg++)
    {
        if (!(regs.dispcnt & (DISPCNT_BG0_ON << bg)))
            continue;
        for (y = 0; y < PORT_TABLE_HEIGHT; y++)
            RenderTextBg(bg, y, 0, 0, PORT_TABLE_WIDTH, TRUE, sBoard[bg - 1][y]);
    }
    memset(sTableSprites, 0, sizeof(sTableSprites));
    memset(sHud, 0, sizeof(sHud));
    memset(sHudRightFrom, -1, sizeof(sHudRightFrom));
    memset(sHudTimerFrom, -1, sizeof(sHudTimerFrom));
    if (regs.dispcnt & DISPCNT_OBJ_ON)
    {
        for (y = 0; y < PORT_TABLE_HEIGHT; y++)
            RenderSprites(PORT_SPRITE_TABLE, y, PORT_TABLE_WIDTH, sTableSprites[y], FALSE);
    }

    // Everything outside the GBA screen: board layers and the table's
    // sprites. The screen part gets drawn over this line by line in
    // PortRenderScanline.
    px[LAYER_BG0] = &sClear;
    for (y = 0; y < height; y++)
    {
        int ty = y - PORT_TABLE_Y;
        for (x = 0; x < width; x++)
        {
            int tx = x - PORT_TABLE_X;
            if (ty < 0 || ty >= PORT_TABLE_HEIGHT || tx < 0 || tx >= PORT_TABLE_WIDTH)
            {
                canvas[y * width + x] = 0xFF000000;
                continue;
            }
            for (bg = 1; bg < 4; bg++)
                px[bg] = &sBoard[bg - 1][ty][tx];
            px[LAYER_OBJ] = &sTableSprites[ty][tx];
            canvas[y * width + x] = Composite(px, &regs);
        }
    }
    KeepTable(&regs);
}

// The last table frame (see PortLastTable).
static uint32_t sLastTable[PORT_TABLE_HEIGHT * PORT_TABLE_WIDTH];
static int sLastTableField = -1;
static int sTableFrames;  // frames since a table appeared

const uint32_t *PortLastTable(int *field)
{
    *field = sLastTableField;
    return sLastTableField >= 0 ? sLastTable : NULL;
}

// Keeps the table part of the canvas (drawn so far: board and table sprites)
// unless a fade is going on. JVPPBPORT_TABLEDUMP=file: the build's way to
// take gPortTablePictures; writes this after 400 frames of a table and quits.
static void KeepTable(const struct BlendRegs *regs)
{
    const char *dump = getenv("JVPPBPORT_TABLEDUMP");
    int y;

    if ((regs->effect == 2 || regs->effect == 3) && regs->evy != 0)
        return;
    for (y = 0; y < PORT_TABLE_HEIGHT; y++)
        memcpy(&sLastTable[y * PORT_TABLE_WIDTH], &sCanvas[(PORT_TABLE_Y + y) * PORT_CANVAS_WIDTH + PORT_TABLE_X],
               PORT_TABLE_WIDTH * 4);
    sLastTableField = gMain.selectedField;
    if (dump && ++sTableFrames == 400)
    {
        FILE *f = fopen(dump, "wb");
        if (!f || fwrite(sLastTable, sizeof(sLastTable), 1, f) != 1)
        {
            fprintf(stderr, "JVPPBPort: can't write %s\n", dump);
            PortExit(1);
        }
        fclose(f);
        PortExit(0);
    }
}

// The HUD: BG0 pixels of the score row (map rows 62-63, shown on the GBA
// screen's last 16 lines by the game's VCount handler) and of the mode timer
// (map rows 11-12, columns 25-28) are not drawn with the rest of the GBA
// screen but where they'd be with the camera at the bottom (PortHudSpot).
// The right half of the score row (Pokémon caught, coins, balls, charge
// icon: map columns 17 and up) is moved 8 pixels further right, to sit
// centered on the red area there (the GBA has it 8 px left).
#define HUD_SCORE_MAP_ROW 62
#define HUD_RIGHT_MAP_COL 17
#define HUD_RIGHT_SHIFT 8
// The full-charge bolt (map column 29, the last) moves only 4: centered
// between the ball count and the table's edge.
#define HUD_BOLT_MAP_COL 29
#define HUD_BOLT_SHIFT 4
#define HUD_TIMER_MAP_ROW 11
#define HUD_TIMER_MAP_COL 25
// The mode timer goes in the table's top-right corner (over part of the
// Mart on Ruby), instead of with the score row. Board
// position of its top-left corner:
#define HUD_TIMER_W 32
#define HUD_TIMER_X (PORT_TABLE_WIDTH - 8 - HUD_TIMER_W)
#define HUD_TIMER_Y 8

// Returns TRUE on the score row's lines (none of BG0 there belongs to the screen).
static int TakeHudPixels(int line, struct Pixel *bg0, const struct BlendRegs *regs)
{
    int hofs = IO16(REG_OFFSET_BG0HOFS) & 0x1FF;
    int mapRow = (((line + IO16(REG_OFFSET_BG0VOFS)) & 0x1FF) >> 3);
    int x, from = 0, to = 0;

    sHudRightFrom[line] = -1;
    sHudTimerFrom[line] = -1;
    if (mapRow == HUD_SCORE_MAP_ROW || mapRow == HUD_SCORE_MAP_ROW + 1)
    {
        to = PORT_SCREEN_WIDTH;
        sHudRightFrom[line] = HUD_RIGHT_MAP_COL * 8 - hofs;
        sHudBoltFrom[line] = HUD_BOLT_MAP_COL * 8 - hofs;
    }
    else if ((mapRow == HUD_TIMER_MAP_ROW || mapRow == HUD_TIMER_MAP_ROW + 1) && PortHudTimerShown())
    {
        from = HUD_TIMER_MAP_COL * 8 - hofs;
        to = from + HUD_TIMER_W;
        sHudTimerFrom[line] = from;
        sHudTimerRow[line] = ((line + IO16(REG_OFFSET_BG0VOFS)) & 0x1FF) - HUD_TIMER_MAP_ROW * 8;
    }
    sHudRegs[line] = *regs;
    for (x = from < 0 ? 0 : from; x < to && x < PORT_SCREEN_WIDTH; x++)
    {
        sHud[line][x] = bg0[x];
        bg0[x].opaque = FALSE;
    }
    return mapRow == HUD_SCORE_MAP_ROW || mapRow == HUD_SCORE_MAP_ROW + 1;
}

static void DrawHud(void)
{
    int width = PortCanvasWidth();
    const struct Pixel *px[5];
    int hudX, hudY, line, x, bg;

    PortHudSpot(&hudX, &hudY);
    for (line = 0; line < PORT_SCREEN_HEIGHT; line++)
    {
        int by = hudY + line;
        if (sHudTimerFrom[line] >= 0)
        {
            // The timer, over the table.
            int ty = HUD_TIMER_Y + sHudTimerRow[line];
            for (x = 0; x < HUD_TIMER_W; x++)
            {
                int sx = sHudTimerFrom[line] + x, tx = HUD_TIMER_X + x;
                if (sx < 0 || sx >= PORT_SCREEN_WIDTH || !sHud[line][sx].opaque)
                    continue;
                px[LAYER_BG0] = &sHud[line][sx];
                for (bg = 1; bg < 4; bg++)
                    px[bg] = (sHudRegs[line].dispcnt & (DISPCNT_BG0_ON << bg)) ? &sBoard[bg - 1][ty][tx] : &sClear;
                px[LAYER_OBJ] = (sHudRegs[line].dispcnt & DISPCNT_OBJ_ON) ? &sTableSprites[ty][tx] : &sClear;
                sCanvas[(PORT_TABLE_Y + ty) * width + PORT_TABLE_X + tx] = Composite(px, &sHudRegs[line]);
            }
            continue;
        }
        if (by < 0 || by >= PORT_TABLE_HEIGHT)
            continue;
        for (x = 0; x < PORT_SCREEN_WIDTH; x++)
        {
            int bx = hudX + x;
            if (sHudRightFrom[line] >= 0 && x >= sHudBoltFrom[line])
                bx += HUD_BOLT_SHIFT;
            else if (sHudRightFrom[line] >= 0 && x >= sHudRightFrom[line])
                bx += HUD_RIGHT_SHIFT;
            if (!sHud[line][x].opaque || bx < 0 || bx >= PORT_TABLE_WIDTH)
                continue;
            px[LAYER_BG0] = &sHud[line][x];
            for (bg = 1; bg < 4; bg++)
                px[bg] = (sHudRegs[line].dispcnt & (DISPCNT_BG0_ON << bg)) ? &sBoard[bg - 1][by][bx] : &sClear;
            px[LAYER_OBJ] = (sHudRegs[line].dispcnt & DISPCNT_OBJ_ON) ? &sTableSprites[by][bx] : &sClear;
            sCanvas[(PORT_TABLE_Y + by) * width + PORT_TABLE_X + bx] = Composite(px, &sHudRegs[line]);
        }
    }
}

// One GBA line of the pause panel when it's drawn apart (sPauseSplit), with
// that line's blend registers, into sPauseLines; PortRenderEnd puts it on.
static void PausePanelLine(int line)
{
    static struct Pixel sprites[PORT_TABLE_WIDTH];
    struct BlendRegs regs;
    const struct Pixel *px[5] = {&sClear, &sClear, &sClear, &sClear, &sClear};
    int x;

    ReadBlendRegs(&regs);
    if ((regs.dispcnt & DISPCNT_FORCED_BLANK) || !(regs.dispcnt & DISPCNT_OBJ_ON))
        return;
    memset(sprites, 0, sizeof(sprites));
    sScreenLeft = sPauseX;
    sScreenWide = TRUE;
    RenderSprites(PORT_SPRITE_PAUSE, line, PORT_TABLE_WIDTH, sprites, FALSE);
    sScreenWide = FALSE;
    for (x = 0; x < PORT_TABLE_WIDTH; x++)
    {
        if (!sprites[x].opaque)
            continue;
        px[LAYER_OBJ] = &sprites[x];
        sPauseLines[line][x] = Composite(px, &regs);
    }
}

static void DrawPausePanel(void)
{
    int width = PortCanvasWidth(), line, x;

    for (line = 0; line < PORT_SCREEN_HEIGHT; line++)
    {
        int cy = PORT_TABLE_Y + sPauseY + line;
        if (cy < 0 || cy >= PortCanvasHeight())
            continue;
        for (x = 0; x < PORT_TABLE_WIDTH; x++)
        {
            int cx = PORT_TABLE_X + x;
            if ((sPauseLines[line][x] >> 24) && cx >= 0 && cx < width)
                sCanvas[cy * width + cx] = sPauseLines[line][x];
        }
    }
}

// Draws line `line` of the GBA screen onto the canvas. Runs after
// PortRenderBegin, once per line, so mid-screen register changes (the game's
// VCount handler) still apply.
void PortRenderScanline(int line)
{
    static struct Pixel layers[2][PORT_TABLE_WIDTH];  // BG0, sprites
    int width = PortCanvasWidth();
    int cy = sWindowY + line, by = sBoardY + line;
    struct BlendRegs regs;
    const struct Pixel *px[5];
    uint32_t *out;
    int x, bg, left;

    if (!sTableMode && sLayout)
    {
        LayoutScanline(line);
        // Lines below the GBA screen (backgrounds are 256 or more lines
        // tall, sprite y goes to 255), for layouts that show some.
        if (line == PORT_SCREEN_HEIGHT - 1)
        {
            int extra;
            for (extra = PORT_SCREEN_HEIGHT; extra < 256; extra++)
                LayoutScanline(extra);
        }
        return;
    }
    if (sTableMode && sPauseSplit)
        PausePanelLine(line);
    if (cy < 0 || cy >= PortCanvasHeight())
        return;
    out = &sCanvas[cy * width];
    if (!sTableMode)
    {
        RenderGbaLine(line, out + sWindowX);
        return;
    }

    ReadBlendRegs(&regs);
    if (regs.dispcnt & DISPCNT_FORCED_BLANK)
        return;  // PortRenderBegin already filled the canvas
    // The GBA screen is 240 wide and the table 256: banners, panels and
    // scene bands (BG0, screen sprites) are drawn across the whole table
    // width, so they reach both edges. layers[][i] is table column i, screen
    // x i - left. BG0's map is 256 wide, so it just wraps on.
    left = sBoardX;
    memset(layers, 0, sizeof(layers));
    if (regs.dispcnt & DISPCNT_BG0_ON)
    {
        RenderTextBg(0, line, (IO16(REG_OFFSET_BG0HOFS) - left) & 0x1FF, IO16(REG_OFFSET_BG0VOFS) & 0x1FF,
                     PORT_TABLE_WIDTH, FALSE, layers[0]);
        if (left >= 0 && left <= PORT_TABLE_WIDTH - PORT_SCREEN_WIDTH
            && TakeHudPixels(line, layers[0] + left, &regs))
        {
            for (x = 0; x < PORT_TABLE_WIDTH; x++)
                if (x < left || x >= left + PORT_SCREEN_WIDTH)
                    layers[0][x].opaque = FALSE;  // the score row's ends aren't screen
        }
    }
    if (regs.dispcnt & DISPCNT_OBJ_ON)
    {
        sScreenLeft = left;
        sScreenWide = TRUE;
        RenderSprites(PORT_SPRITE_SCREEN, line, PORT_TABLE_WIDTH, layers[1], FALSE);
        sScreenWide = FALSE;
    }

    // Where the GBA left nothing past its edges (the game draws for 240
    // columns), the screen is carried on out to the table's edges: bars,
    // bands and panels (BG0) by repeating their outer 16 columns (two tiles,
    // so patterns go on), banners (sprites) by repeating their edge column
    // (their border), and only once they reach both edges (fully in place).
    if (left >= 0 && left <= PORT_TABLE_WIDTH - PORT_SCREEN_WIDTH)
    {
        int right = left + PORT_SCREEN_WIDTH - 1;
        int both = layers[1][left].opaque && layers[1][right].opaque;
        for (x = right + 1; x < PORT_TABLE_WIDTH; x++)
        {
            if (!layers[0][x].opaque && layers[0][x - 16].opaque)
                layers[0][x] = layers[0][x - 16];
            if (both && !layers[1][x].opaque)
                layers[1][x] = layers[1][right];
        }
        for (x = left - 1; x >= 0; x--)
        {
            if (!layers[0][x].opaque && layers[0][x + 16].opaque)
                layers[0][x] = layers[0][x + 16];
            if (both && !layers[1][x].opaque)
                layers[1][x] = layers[1][left];
        }
    }

    for (x = 0; x < PORT_TABLE_WIDTH; x++)
    {
        int cx = PORT_TABLE_X + x, bx = x;
        int onBoard = by >= 0 && by < PORT_TABLE_HEIGHT;
        const struct Pixel *screenSprite = &layers[1][x];

        if (cx < 0 || cx >= width)
            continue;
        // Screen sprites and table sprites: the front-most one, as on the GBA,
        // while the GBA screen is drawn at the game's camera. Anywhere else
        // (pop-ups in the middle of the table) the table sprites under it
        // aren't the ones the GBA shows with it, so the screen's go on top
        // (Ruby's Sharpedo was over GAME OVER's R).
        if (onBoard && (regs.dispcnt & DISPCNT_OBJ_ON))
        {
            const struct Pixel *tableSprite = &sTableSprites[by][bx];
            if (tableSprite->opaque && (!screenSprite->opaque
                                        || (sPopupAtCamera
                                            && (tableSprite->priority < screenSprite->priority
                                                || (tableSprite->priority == screenSprite->priority
                                                    && tableSprite->index < screenSprite->index)))))
                screenSprite = tableSprite;
        }
        px[LAYER_BG0] = &layers[0][x];
        px[LAYER_OBJ] = screenSprite;
        for (bg = 1; bg < 4; bg++)
            px[bg] = onBoard && (regs.dispcnt & (DISPCNT_BG0_ON << bg)) ? &sBoard[bg - 1][by][bx] : &sClear;
        out[cx] = Composite(px, &regs);
    }
    if (line == PORT_SCREEN_HEIGHT - 1)
        DrawHud();
}

// ---- Positions for the port's text and the mouse ----

// Where GBA screen position (x, y) was drawn on the canvas in the last
// picture: FALSE if that part of the screen isn't shown.
int PortScreenToCanvas(int x, int y, int *cx, int *cy)
{
    int i;

    if (sLayout && !sTableMode)
    {
        for (i = 0; i < sLayout->pieceCount; i++)
        {
            const struct PortLayoutPiece *pc = &sLayout->pieces[i];
            if (x >= pc->sx && x < pc->sx + pc->w && y >= pc->sy && y < pc->sy + pc->h)
            {
                *cx = pc->dx + x - pc->sx;
                *cy = pc->dy + y - pc->sy;
                return TRUE;
            }
        }
        return FALSE;
    }
    *cx = sWindowX + x;
    *cy = sWindowY + y;
    return TRUE;
}

// The GBA screen position shown at canvas position (cx, cy) in the last
// picture: FALSE if none is shown there. With a layout, positions below the
// GBA screen (lines 160-255) can come out too.
int PortCanvasToScreen(int cx, int cy, int *x, int *y)
{
    int i;

    if (sLayout && !sTableMode)
    {
        for (i = sLayout->pieceCount - 1; i >= 0; i--)
        {
            const struct PortLayoutPiece *pc = &sLayout->pieces[i];
            if (cx >= pc->dx && cx < pc->dx + pc->w && cy >= pc->dy && cy < pc->dy + (pc->dh ? pc->dh : pc->h))
            {
                *x = pc->sx + cx - pc->dx;
                *y = pc->sy + (cy - pc->dy) % pc->h;
                return TRUE;
            }
        }
        return FALSE;
    }
    *x = cx - sWindowX;
    *y = cy - sWindowY;
    return *x >= 0 && *x < PORT_SCREEN_WIDTH && *y >= 0 && *y < PORT_SCREEN_HEIGHT;
}

// A color for the port's own text, with the screen's fade applied.
uint32_t PortFadeColor(uint32_t argb)
{
    struct BlendRegs regs;

    ReadBlendRegs(&regs);
    return FadeArgb(argb, &regs);
}
