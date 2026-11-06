// JVPPBPort: a small pixel font (capitals, digits, a few signs; 7 pixels
// tall) for text the port writes over the game's screens, such as the key
// names on the Options screen. Text is queued in GBA screen coordinates
// while the game runs a frame (PortDrawText) and drawn over that frame's
// picture once it is finished (PortDrawTexts), following screen fades.
#include <string.h>
#include "jvppbport.h"

#define GLYPH_H 7
#define MAX_TEXTS 16

struct Glyph
{
    char c;
    const char *rows[GLYPH_H];
};

static const struct Glyph sGlyphs[] = {
    {' ', {"...", "...", "...", "...", "...", "...", "..."}},
    {'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
    {'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}},
    {'D', {"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}},
    {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
    {'F', {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}},
    {'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"}},
    {'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'I', {"###", ".#.", ".#.", ".#.", ".#.", ".#.", "###"}},
    {'J', {"..###", "...#.", "...#.", "...#.", "#..#.", "#..#.", ".##.."}},
    {'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
    {'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
    {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
    {'N', {"#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#"}},
    {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
    {'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
    {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
    {'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'V', {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
    {'W', {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#."}},
    {'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
    {'Y', {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}},
    {'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
    {'0', {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}},
    {'1', {".#.", "##.", ".#.", ".#.", ".#.", ".#.", "###"}},
    {'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
    {'3', {"####.", "....#", "....#", ".###.", "....#", "....#", "####."}},
    {'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
    {'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
    {'6', {".###.", "#....", "#....", "####.", "#...#", "#...#", ".###."}},
    {'7', {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}},
    {'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
    {'9', {".###.", "#...#", "#...#", ".####", "....#", "....#", ".###."}},
    {',', {"..", "..", "..", "..", "..", ".#", "#."}},
    {'.', {".", ".", ".", ".", ".", ".", "#"}},
    {'/', {"....#", "....#", "...#.", "..#..", ".#...", "#....", "#...."}},
    {'\\', {"#....", "#....", ".#...", "..#..", "...#.", "....#", "....#"}},
    {';', {"..", "..", ".#", "..", "..", ".#", "#."}},
    {':', {".", ".", "#", ".", ".", "#", "."}},
    {'\'', {"#", "#", ".", ".", ".", ".", "."}},
    {'`', {"#.", ".#", "..", "..", "..", "..", ".."}},
    {'[', {"###", "#..", "#..", "#..", "#..", "#..", "###"}},
    {']', {"###", "..#", "..#", "..#", "..#", "..#", "###"}},
    {'-', {"....", "....", "....", "####", "....", "....", "...."}},
    {'=', {"....", "....", "####", "....", "####", "....", "...."}},
    {'+', {".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."}},
    {'?', {".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.."}},
    {'!', {"#", "#", "#", "#", "#", ".", "#"}},
    {'&', {".##..", "#..#.", "#.#..", ".#...", "#.#.#", "#..#.", ".##.#"}},
    {'(', {".#", "#.", "#.", "#.", "#.", "#.", ".#"}},
    {')', {"#.", ".#", ".#", ".#", ".#", ".#", "#."}},
};

static struct
{
    int x, y;
    uint32_t color;
    int font, scale;  // font -1: the small font above; else PORT_FONT_* (scale: see PortDrawGameText)
    char s[32];
} sTexts[MAX_TEXTS];
static int sTextCount;

static const struct Glyph *FindGlyph(char c)
{
    size_t i;

    if (c >= 'a' && c <= 'z')
        c -= 'a' - 'A';
    for (i = 0; i < sizeof(sGlyphs) / sizeof(sGlyphs[0]); i++)
    {
        if (sGlyphs[i].c == c)
            return &sGlyphs[i];
    }
    return FindGlyph('?');
}

int PortTextWidth(const char *s)
{
    int w = 0;

    for (; *s; s++)
        w += strlen(FindGlyph(*s)->rows[0]) + 1;
    return w ? w - 1 : 0;
}

void PortDrawText(int x, int y, const char *s, uint32_t color)
{
    if (sTextCount == MAX_TEXTS)
        return;
    sTexts[sTextCount].x = x;
    sTexts[sTextCount].y = y;
    sTexts[sTextCount].color = color;
    sTexts[sTextCount].font = -1;
    strncpy(sTexts[sTextCount].s, s, sizeof(sTexts[0].s) - 1);
    sTexts[sTextCount].s[sizeof(sTexts[0].s) - 1] = '\0';
    sTextCount++;
}

static uint32_t *sPutCanvas;

// One pixel of game-style text, at a GBA screen position.
static void PutOnScreen(int x, int y, uint32_t color)
{
    int cx, cy;

    if (PortScreenToCanvas(x, y, &cx, &cy) && cx >= 0 && cx < PortCanvasWidth() && cy >= 0 && cy < PortCanvasHeight())
        sPutCanvas[cy * PortCanvasWidth() + cx] = color;
}

void PortDrawTexts(uint32_t *canvas)
{
    int width = PortCanvasWidth(), height = PortCanvasHeight();
    int i, row, col;

    sPutCanvas = canvas;
    for (i = 0; i < sTextCount; i++)
    {
        uint32_t color = PortFadeColor(sTexts[i].color);
        int x = sTexts[i].x;
        const char *s;

        if (sTexts[i].font >= 0)
        {
            PortDrawFontText(sTexts[i].font, x, sTexts[i].y, sTexts[i].s, sTexts[i].scale, PutOnScreen);
            continue;
        }

        for (s = sTexts[i].s; *s; s++)
        {
            const struct Glyph *g = FindGlyph(*s);
            int w = strlen(g->rows[0]);
            for (row = 0; row < GLYPH_H; row++)
            {
                for (col = 0; col < w; col++)
                {
                    int cx, cy;
                    if (g->rows[row][col] != '#')
                        continue;
                    if (PortScreenToCanvas(x + col, sTexts[i].y + row, &cx, &cy)
                        && cx >= 0 && cx < width && cy >= 0 && cy < height)
                        canvas[cy * width + cx] = color;
                }
            }
            x += w + 1;
        }
    }
    sTextCount = 0;
}

// Draws text straight onto a canvas (not the GBA screen), each font pixel
// as a scale x scale square. Used by the setup screen.
void PortDrawTextOn(uint32_t *canvas, int width, int height, int x, int y, const char *s, uint32_t color, int scale)
{
    int row, col, dx, dy;

    for (; *s; s++)
    {
        const struct Glyph *g = FindGlyph(*s);
        int w = strlen(g->rows[0]);
        for (row = 0; row < GLYPH_H; row++)
        {
            for (col = 0; col < w; col++)
            {
                if (g->rows[row][col] != '#')
                    continue;
                for (dy = 0; dy < scale; dy++)
                {
                    for (dx = 0; dx < scale; dx++)
                    {
                        int cx = x + col * scale + dx, cy = y + row * scale + dy;
                        if (cx >= 0 && cx < width && cy >= 0 && cy < height)
                            canvas[cy * width + cx] = color;
                    }
                }
            }
        }
        x += (w + 1) * scale;
    }
}

// ---- Game-style text ----
// A second font, 5 pixels tall, drawn the way the game's own menu labels
// are: white 1-pixel strokes, everything next to a stroke (also diagonally)
// black, so each word sits in a black outline. Letters are 1 pixel apart,
// and that pixel becomes outline. Used for the port's rows on the Options
// screen (ppu.c), next to the game's own labels.
#define BOLD_H 5

static const struct
{
    char c;
    const char *rows[BOLD_H];
} sBold[] = {
    {' ', {"..", "..", "..", "..", ".."}},
    {'A', {"###", "#.#", "###", "#.#", "#.#"}},
    {'B', {"##.", "#.#", "##.", "#.#", "##."}},
    {'C', {"###", "#..", "#..", "#..", "###"}},
    {'D', {"##.", "#.#", "#.#", "#.#", "##."}},
    {'E', {"###", "#..", "###", "#..", "###"}},
    {'F', {"###", "#..", "###", "#..", "#.."}},
    {'G', {"###", "#..", "#.#", "#.#", "###"}},
    {'H', {"#.#", "#.#", "###", "#.#", "#.#"}},
    {'I', {"#", "#", "#", "#", "#"}},
    {'J', {"..#", "..#", "..#", "#.#", "###"}},
    {'K', {"#.#", "#.#", "##.", "#.#", "#.#"}},
    {'L', {"#..", "#..", "#..", "#..", "###"}},
    {'M', {"#...#", "##.##", "#.#.#", "#...#", "#...#"}},
    {'N', {"#..#", "##.#", "#.##", "#..#", "#..#"}},
    {'O', {"###", "#.#", "#.#", "#.#", "###"}},
    {'P', {"###", "#.#", "###", "#..", "#.."}},
    {'Q', {"###", "#.#", "#.#", "###", "..#"}},
    {'R', {"###", "#.#", "##.", "#.#", "#.#"}},
    {'S', {"###", "#..", "###", "..#", "###"}},
    {'T', {"###", ".#.", ".#.", ".#.", ".#."}},
    {'U', {"#.#", "#.#", "#.#", "#.#", "###"}},
    {'V', {"#.#", "#.#", "#.#", "#.#", ".#."}},
    {'W', {"#...#", "#...#", "#.#.#", "##.##", "#...#"}},
    {'X', {"#.#", "#.#", ".#.", "#.#", "#.#"}},
    {'Y', {"#.#", "#.#", ".#.", ".#.", ".#."}},
    {'Z', {"###", "..#", ".#.", "#..", "###"}},
    {'0', {"###", "#.#", "#.#", "#.#", "###"}},
    {'1', {"##.", ".#.", ".#.", ".#.", "###"}},
    {'2', {"###", "..#", "###", "#..", "###"}},
    {'3', {"###", "..#", "###", "..#", "###"}},
    {'4', {"#.#", "#.#", "###", "..#", "..#"}},
    {'5', {"###", "#..", "###", "..#", "###"}},
    {'6', {"###", "#..", "###", "#.#", "###"}},
    {'7', {"###", "..#", "..#", "..#", "..#"}},
    {'8', {"###", "#.#", "###", "#.#", "###"}},
    {'9', {"###", "#.#", "###", "..#", "###"}},
    // Signs, for key names (MENU KEYS).
    {',', {".", ".", ".", "#", "#"}},
    {'.', {".", ".", ".", ".", "#"}},
    {'?', {"###", "..#", ".##", "...", ".#."}},
    {'-', {"...", "...", "###", "...", "..."}},
    {'+', {"...", ".#.", "###", ".#.", "..."}},
    {'*', {"#.#", ".#.", "###", ".#.", "#.#"}},
    {'=', {"...", "###", "...", "###", "..."}},
    {'[', {"##", "#.", "#.", "#.", "##"}},
    {']', {"##", ".#", ".#", ".#", "##"}},
    {'/', {"..#", "..#", ".#.", "#..", "#.."}},
    {'\\', {"#..", "#..", ".#.", "..#", "..#"}},
    {';', {".", "#", ".", "#", "#"}},
    {'\'', {"#", "#", ".", ".", "."}},
    {'`', {"#.", ".#", "..", "..", ".."}},
};

static int BoldGlyph(char c)
{
    size_t i;

    if (c >= 'a' && c <= 'z')
        c -= 'a' - 'A';
    for (i = 0; i < sizeof(sBold) / sizeof(sBold[0]); i++)
        if (sBold[i].c == c)
            return (int)i;
    return 0;
}

int PortBoldTextWidth(const char *s)
{
    int w = 0;

    for (; *s; s++)
        w += strlen(sBold[BoldGlyph(*s)].rows[0]) + 1;
    return w ? w - 1 : 0;
}

// Draws `s` with its strokes' top-left corner at (x, y); the outline takes
// one more pixel all around. `put` writes one pixel (it may clip or fade).
void PortDrawBoldText(int x, int y, const char *s, uint32_t stroke, uint32_t outline,
                      void (*put)(int x, int y, uint32_t color))
{
    enum { MAX_W = 160 };
    static uint8_t mask[BOLD_H + 2][MAX_W + 2];
    int w = PortBoldTextWidth(s), cx = 1, row, col, dx, dy;

    if (w > MAX_W)
        return;
    memset(mask, 0, sizeof(mask));
    for (; *s; s++)
    {
        int g = BoldGlyph(*s), gw = strlen(sBold[g].rows[0]);
        for (row = 0; row < BOLD_H; row++)
            for (col = 0; col < gw; col++)
                if (sBold[g].rows[row][col] == '#')
                    mask[row + 1][cx + col] = 2;
        cx += gw + 1;
    }
    for (row = 0; row < BOLD_H + 2; row++)
    {
        for (col = 0; col < w + 2; col++)
        {
            int near = 0;
            if (mask[row][col] == 2)
            {
                put(x - 1 + col, y - 1 + row, stroke);
                continue;
            }
            for (dy = -1; dy <= 1; dy++)
                for (dx = -1; dx <= 1; dx++)
                    if (row + dy >= 0 && row + dy < BOLD_H + 2 && col + dx >= 0 && col + dx < w + 2
                        && mask[row + dy][col + dx] == 2)
                        near = 1;
            if (near)
                put(x - 1 + col, y - 1 + row, outline);
        }
    }
}

// ---- The game's word styles ----
// Fonts for port words that sit next to the game's own (drawn here, not
// taken from the game): PORT_FONT_MENU like the main menu's items (black
// 2-pixel strokes, a white outline all around, 7 lines tall; only EXIT's
// letters so far). Fonts may be up to FONT_MAX_H tall, with or without an
// outline.
#define FONT_MAX_H 8

struct FontGlyph
{
    char c;
    const char *rows[FONT_MAX_H];
};

static const struct FontGlyph sMenuGlyphs[] = {
    {' ', {"...", "...", "...", "...", "...", "...", "..."}},
    {'E', {"#####", "##...", "##...", "####.", "##...", "##...", "#####"}},
    {'I', {"##", "##", "##", "##", "##", "##", "##"}},
    {'T', {"######", "..##..", "..##..", "..##..", "..##..", "..##..", "..##.."}},
    {'X', {"##..##", "##..##", ".####.", "..##..", ".####.", "##..##", "##..##"}},
};

// PORT_FONT_LABEL: like Field Select's NORMAL / SLOW (white 1-pixel
// strokes, a black outline all around, 5 lines tall; only FAST's letters).
static const struct FontGlyph sLabelGlyphs[] = {
    {' ', {"...", "...", "...", "...", "..."}},
    {'A', {".##.", "#..#", "####", "#..#", "#..#"}},
    {'F', {"####", "#...", "###.", "#...", "#..."}},
    {'S', {".###", "#...", ".##.", "...#", "###."}},
    {'T', {"###", ".#.", ".#.", ".#.", ".#."}},
};

static const struct
{
    const struct FontGlyph *glyphs;
    int count, height, gap;
    uint32_t stroke, outline;  // outline 0: none
} sFonts[] = {
    [PORT_FONT_MENU] = {sMenuGlyphs, sizeof(sMenuGlyphs) / sizeof(sMenuGlyphs[0]), 7, 1, 0xFF000000, 0xFFFFFFFF},
    [PORT_FONT_LABEL] = {sLabelGlyphs, sizeof(sLabelGlyphs) / sizeof(sLabelGlyphs[0]), 5, 1, 0xFFFFFFFF, 0xFF000000},
};

static const struct FontGlyph *FontGlyph(int font, char c)
{
    int i;

    for (i = 0; i < sFonts[font].count; i++)
        if (sFonts[font].glyphs[i].c == c)
            return &sFonts[font].glyphs[i];
    return &sFonts[font].glyphs[0];
}

int PortFontTextWidth(int font, const char *s)
{
    int w = 0;

    for (; *s; s++)
        w += strlen(FontGlyph(font, *s)->rows[0]) + sFonts[font].gap;
    return w ? w - sFonts[font].gap : 0;
}

// Draws `s` with its strokes' top-left corner at (x, y) (an outline takes
// one more pixel all around), squeezed to scale/256 of its height around
// its middle. `put` writes a pixel.
void PortDrawFontText(int font, int x, int y, const char *s, int scale, void (*put)(int x, int y, uint32_t color))
{
    enum { MAX_W = 160 };
    static uint8_t mask[FONT_MAX_H + 2][MAX_W + 2];
    int h = sFonts[font].height, w = PortFontTextWidth(font, s), cx = 1, row, col, dx, dy, d;
    int maskH = h + 2, mid = maskH / 2;

    if (w > MAX_W || scale <= 0)
        return;
    memset(mask, 0, sizeof(mask));
    for (; *s; s++)
    {
        const struct FontGlyph *g = FontGlyph(font, *s);
        int gw = strlen(g->rows[0]);
        for (row = 0; row < h; row++)
            for (col = 0; col < gw; col++)
                if (g->rows[row][col] == '#')
                    mask[row + 1][cx + col] = 2;
        cx += gw + sFonts[font].gap;
    }
    if (sFonts[font].outline)
    {
        for (row = 0; row < maskH; row++)
            for (col = 0; col < w + 2; col++)
                if (mask[row][col] != 2)
                    for (dy = -1; dy <= 1; dy++)
                        for (dx = -1; dx <= 1; dx++)
                            if (row + dy >= 0 && row + dy < maskH && col + dx >= 0 && col + dx < w + 2
                                && mask[row + dy][col + dx] == 2)
                                mask[row][col] = 1;
    }
    for (d = -maskH; d <= maskH; d++)
    {
        int src = mid + (d >= 0 ? d * 256 / scale : -((-d * 256 + scale - 1) / scale));
        if (src < 0 || src >= maskH)
            continue;
        for (col = 0; col < w + 2; col++)
            if (mask[src][col])
                put(x - 1 + col, y - 1 + mid + d, mask[src][col] == 2 ? sFonts[font].stroke : sFonts[font].outline);
    }
}

// Queues game-style text at a GBA screen position, drawn over the finished
// frame like PortDrawText's. scale: 256 = full height.
void PortDrawGameText(int font, int x, int y, const char *s, int scale)
{
    if (sTextCount == MAX_TEXTS)
        return;
    PortDrawText(x, y, s, 0);
    sTexts[sTextCount - 1].font = font;
    sTexts[sTextCount - 1].scale = scale;
}
