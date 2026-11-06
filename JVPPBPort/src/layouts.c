// JVPPBPort: layouts for the short screens in the tall window. ppu.c
// draws a screen that has a layout piece by piece (struct PortLayout in
// jvppbport.h) instead of centering the GBA's 240x160 picture. Only drawing
// changes; the game runs exactly as on the GBA.
#include "jvppbport_layout.h"
#include <math.h>
#include <string.h>
#include "global.h"
#include "main.h"
#include "constants/fields.h"
#include "titlescreen.h"
#include "jvppbport.h"

// ---- Bigger menu canvases ----
// On a touch screen the menus' canvas has the screen's shape (ppu.c:
// PortMenuSize), taller and/or wider than the 256x448 the layouts are
// written for (menus fill the whole screen). Spread()
// fits a layout to it: things are centered across the extra width, and the
// extra height is shared out evenly between the gaps above, between and
// below the layout's bands (rows of things, split at the given y values).
// Background fills and tiles that reach an edge of the canvas grow with it,
// so backdrops cover it all; pictures and rectangles keep their size, except
// the first `placed` images, already made for the canvas's real size (Field
// Select's border).
#define EDGE 12
#define MAX_SPREAD_PIECES 16
#define MAX_SPREAD_IMAGES 32
#define MAX_SPREAD_TILES 4

static int sExtraW, sExtraH;
static const int16_t *sSplits;
static int sSplitCount;

// How far a y (in the 256x448 layout) moves down.
static int ShiftY(int y)
{
    int band = 0;

    while (band < sSplitCount && y >= sSplits[band])
        band++;
    return sExtraH * (band + 1) / (sSplitCount + 2);
}

// The new position of an edge at `pos` of a canvas side `size` long (extra
// pixels more now); `inside`: how far an inner edge moves.
static int MoveEdge(int pos, int size, int extra, int inside)
{
    if (pos <= EDGE)
        return pos;
    if (pos >= size - EDGE)
        return pos + extra;
    return pos + inside;
}

// A rectangle that may grow: each edge moves on its own.
static void MoveRect(int16_t *x, int16_t *y, int16_t *w, int16_t *h)
{
    int x0 = MoveEdge(*x, PORT_CANVAS_WIDTH, sExtraW, sExtraW / 2);
    int x1 = MoveEdge(*x + *w, PORT_CANVAS_WIDTH, sExtraW, sExtraW / 2);
    int y0 = MoveEdge(*y, PORT_CANVAS_HEIGHT, sExtraH, ShiftY(*y));
    int y1 = MoveEdge(*y + *h, PORT_CANVAS_HEIGHT, sExtraH, ShiftY(*y + *h - 1));

    *x = x0;
    *y = y0;
    *w = x1 - x0;
    *h = y1 - y0;
}

// A thing of fixed size (a piece of the GBA screen, a picture): only moves.
static void MovePoint(int16_t *x, int16_t *y)
{
    *x += sExtraW / 2;
    *y += ShiftY(*y);
}

static const struct PortLayout *Spread(const struct PortLayout *in, const int16_t *splits, int splitCount, int placed)
{
    static struct PortLayout out;
    static struct PortLayoutPiece pieces[MAX_SPREAD_PIECES];
    static struct PortLayoutImage images[MAX_SPREAD_IMAGES];
    static struct PortLayoutTile tiles[MAX_SPREAD_TILES];
    int i;

    sExtraW = PortCanvasWidth() - PORT_CANVAS_WIDTH;
    sExtraH = PortCanvasHeight() - PORT_CANVAS_HEIGHT;
    sSplits = splits;
    sSplitCount = splitCount;
    if ((sExtraW == 0 && sExtraH == 0) || in->pieceCount > MAX_SPREAD_PIECES
        || in->imageCount > MAX_SPREAD_IMAGES || in->tileCount > MAX_SPREAD_TILES)
        return in;
    out = *in;
    for (i = 0; i < in->pieceCount; i++)
    {
        pieces[i] = in->pieces[i];
        MovePoint(&pieces[i].dx, &pieces[i].dy);
    }
    for (i = 0; i < in->imageCount; i++)
    {
        images[i] = in->images[i];
        if (i >= placed)
            MovePoint(&images[i].dx, &images[i].dy);
    }
    for (i = 0; i < in->tileCount; i++)
    {
        tiles[i] = in->tiles[i];
        MoveRect(&tiles[i].dx, &tiles[i].dy, &tiles[i].dw, &tiles[i].dh);
    }
    out.pieces = pieces;
    out.images = images;
    out.tiles = tiles;
    return &out;
}

// Title screen and main menu (one screen in the game). BG1 holds the
// striped backdrop and the logo; PRESS START and the menu are sprites, the
// menu box half see-through over BG1. The logo moves up, PRESS START and the
// menu move down (menu box over the stripes instead of the logo), and the
// stripes fill the rest.
#define TITLE_LOGO_Y 48
#define TITLE_PRESS_START_Y 368
#define TITLE_MENU_Y 256  // top of the (no saved game) menu box; it ends above PRESS START, which stays visible, greyed, as on the GBA

static const struct PortLayoutPiece sTitlePieces[] = {
    // Logo block: mascots, PINBALL, RUBY & SAPPHIRE.
    {0, 0, 240, 138, 8, TITLE_LOGO_Y, PORT_LAYER_BG(1), FALSE},
    // Menu box (lines 24-137 of the GBA screen), over the stripes.
    {0, 24, 240, 114, 8, TITLE_MENU_Y - 43 + 24, PORT_LAYER_OBJ, TRUE},
    // PRESS START (lines 138-159).
    {0, 138, 240, 22, 8, TITLE_PRESS_START_Y - 5, PORT_LAYER_OBJ, TRUE},
};

static const int16_t sTitleSplits[] = {200, 360};  // logo, menu, PRESS START

static const struct PortLayout sTitleLayout = {
    .fillBg = 1, .fillX = 0, .fillY = 0, .fillW = 8, .fillH = 8,
    .pieceCount = ARRAY_COUNT(sTitlePieces), .pieces = sTitlePieces,
};

// Touch screens: the menu's items further apart, easier to hit (items at
// their original size). The box is cut into one strip per
// item, split halfway between items (a blank line of the box), and that
// blank line is stretched TITLE_MENU_GAP lines between the strips. The
// taller box stays centered where the GBA's box is.
#define TITLE_MENU_GAP 8
#define TITLE_MENU_TOP 24     // the menu piece's source lines (see sTitlePieces)
#define TITLE_MENU_BOTTOM 138
extern const struct VectorU16 gTitleNoSaveSelectorPositions[4];
extern const struct VectorU16 gTitleSavedSelectorPositions[5];
extern struct TitlescreenStruct gTitlescreen;

// PC: an EXIT item under the game's last one (the game's menu has no room
// for it). The box is cut just under the last item, its
// blank line there stretched one item taller, and the EXIT row (its cursor
// sprites, put by titlescreen.c on GBA lines TITLE_EXIT_LINES, under the
// screen) laid over that; the taller box stays centered. titlescreen.c
// writes the word.
#define TITLE_EXIT_LINES 176

static int TitleItemGap(int saved)
{
    const struct VectorU16 *pos = saved ? gTitleSavedSelectorPositions : gTitleNoSaveSelectorPositions;

    return pos[1].y - pos[0].y;
}

// The source line where the box is cut: the blank line under the last item.
static int TitleExitCut(int saved)
{
    const struct VectorU16 *pos = saved ? gTitleSavedSelectorPositions : gTitleNoSaveSelectorPositions;

    return pos[saved ? 4 : 3].y + 10;
}

int PortTitleExitY(int saved)
{
    const struct VectorU16 *pos = saved ? gTitleSavedSelectorPositions : gTitleNoSaveSelectorPositions;

    return TITLE_EXIT_LINES + pos[saved ? 4 : 3].y + TitleItemGap(saved) - TitleExitCut(saved);
}

static const struct PortLayout *TitleExitLayout(void)
{
    static struct PortLayoutPiece pieces[6];
    static struct PortLayout layout;
    int saved = gTitlescreen.hasSavedGame, gap = TitleItemGap(saved), cut = TitleExitCut(saved);
    int dy = sTitlePieces[1].dy - gap / 2;

    pieces[0] = sTitlePieces[0];
    pieces[1] = sTitlePieces[1];  // down to the cut
    pieces[1].h = cut - TITLE_MENU_TOP;
    pieces[1].dy = dy;
    pieces[2] = sTitlePieces[1];  // the blank line, stretched
    pieces[2].sy = cut;
    pieces[2].h = 1;
    pieces[2].dh = gap;
    pieces[2].dy = dy + cut - TITLE_MENU_TOP;
    pieces[3] = sTitlePieces[1];  // the rest of the box
    pieces[3].sy = cut;
    pieces[3].h = TITLE_MENU_BOTTOM - cut;
    pieces[3].dy = pieces[2].dy + gap;
    pieces[4] = sTitlePieces[1];  // the EXIT row's cursor, over the stretched line
    pieces[4].sy = TITLE_EXIT_LINES;
    pieces[4].h = gap;
    pieces[4].dy = pieces[2].dy;
    pieces[5] = sTitlePieces[2];
    layout = sTitleLayout;
    layout.pieces = pieces;
    layout.pieceCount = 6;
    return &layout;
}

static const struct PortLayout *TitleLayout(void)
{
    static struct PortLayoutPiece pieces[2 + 2 * 5];
    static struct PortLayout layout;
    const struct VectorU16 *pos = gTitlescreen.hasSavedGame ? gTitleSavedSelectorPositions : gTitleNoSaveSelectorPositions;
    int count = gTitlescreen.hasSavedGame ? 5 : 4;
    int n = 0, i, top = TITLE_MENU_TOP, dy;

    if (PortPcMenus())
        return TitleExitLayout();
    if (!PortTouchEnabled())
        return &sTitleLayout;
    dy = sTitlePieces[1].dy - (count - 1) * TITLE_MENU_GAP / 2;
    pieces[n++] = sTitlePieces[0];
    for (i = 0; i < count; i++)
    {
        // Each item's text and cursor are 8 lines from its selector position.
        int bottom = i < count - 1 ? (pos[i].y + pos[i + 1].y) / 2 + 4 : TITLE_MENU_BOTTOM;

        pieces[n] = sTitlePieces[1];
        pieces[n].sy = top;
        pieces[n].h = bottom - top;
        pieces[n++].dy = dy;
        dy += bottom - top;
        if (i < count - 1)
        {
            pieces[n] = sTitlePieces[1];
            pieces[n].sy = bottom;
            pieces[n].h = 1;
            pieces[n].dh = TITLE_MENU_GAP;
            pieces[n++].dy = dy;
            dy += TITLE_MENU_GAP;
        }
        top = bottom;
    }
    pieces[n++] = sTitlePieces[2];
    layout = sTitleLayout;
    layout.pieces = pieces;
    layout.pieceCount = n;
    return &layout;
}

// Field Select: each whole table, a bit under half size, side by side with
// a gap (gPortTablePictures, made at the first start), each framed
// and with the game's orange name tab (BG0 of the GBA's card) sitting on
// it; the unselected one dimmed. The game's Ball Speed box (original size)
// is centered below.
int PortFieldSelectChoice(int *ballSpeedShown);  // field_select.c

#define FS_MARGIN 8
#define FS_PREVIEW_W ((PORT_CANVAS_WIDTH - 3 * FS_MARGIN) / 2)
#define FS_PREVIEW_H (PORT_TABLE_HEIGHT * FS_PREVIEW_W / PORT_TABLE_WIDTH)
#define FS_PREVIEW_X(field) (FS_MARGIN + (field) * (FS_PREVIEW_W + FS_MARGIN))
#define FS_PREVIEW_Y 48
#define FS_TAB_X 44   // the tab on the GBA's Ruby card; Sapphire's is 104 to the right
#define FS_TAB_Y 27
#define FS_TAB_W 50
#define FS_TAB_H 12
#define FS_TAB_DX(field) (FS_PREVIEW_X(field) + (FS_PREVIEW_W - FS_TAB_W) / 2)
#define FS_SPEED_Y (FS_PREVIEW_Y + FS_PREVIEW_H + 24)
#define FS_SPEED_W 88
#define FS_SPEED_H 36
#define FS_ORANGE 0xFFE89028
#define FS_DARK 0xFF282830
#define FS_DIM 110   // brightness of the unselected table, out of 256
#define FS_DIM_EVY 9 // about the same, as a GBA-style darkening
// The screen's border, as on the GBA: grey, a light green line, green.
static const uint32_t sFieldBorder[3][2] = {{0xFF5A5A5A, 4}, {0xFFADFFC6, 1}, {0xFF42E773, 5}};

static struct PortLayoutPiece sFieldPieces[] = {
    // FIELD SELECT header.
    {0, 0, 240, 14, 8, 16, PORT_LAYER_BG(0) | PORT_LAYER_BG(1) | PORT_LAYER_BG(2), FALSE},
    // Name tabs, hanging from the top of the frames onto the tables.
    {FS_TAB_X, FS_TAB_Y, FS_TAB_W, FS_TAB_H, FS_TAB_DX(0), FS_PREVIEW_Y - 2, PORT_LAYER_BG(0), TRUE},
    {FS_TAB_X + 104, FS_TAB_Y, FS_TAB_W, FS_TAB_H, FS_TAB_DX(1), FS_PREVIEW_Y - 2, PORT_LAYER_BG(0), TRUE},
    // Ball Speed box (its source moves with the selected card); the last piece.
    {0, 70, FS_SPEED_W, FS_SPEED_H, (PORT_CANVAS_WIDTH - FS_SPEED_W) / 2, FS_SPEED_Y, PORT_LAYER_OBJ, TRUE, TRUE},
};

static struct PortLayoutImage sFieldImages[12 + 2 * 5 + 2];
static const int16_t sFieldSplits[] = {40, 262};  // header, tables, Ball Speed
#define FS_BORDER_IMAGES 12

static struct PortLayout sFieldLayout = {
    .fillBg = 0, .fillX = 16, .fillY = 56, .fillW = 8, .fillH = 8,
    .pieces = sFieldPieces, .images = sFieldImages,
};

static void SetRect(struct PortLayoutImage *im, int x, int y, int w, int h, uint32_t color)
{
    *im = (struct PortLayoutImage){.color = color, .w = w, .h = h, .dx = x, .dy = y, .brightness = 256};
}

// A 2-pixel frame just outside the rectangle.
static int SetFrame(struct PortLayoutImage *im, int x, int y, int w, int h, uint32_t color, int brightness)
{
    SetRect(&im[0], x - 2, y - 2, w + 4, 2, color);
    SetRect(&im[1], x - 2, y + h, w + 4, 2, color);
    SetRect(&im[2], x - 2, y, 2, h, color);
    SetRect(&im[3], x + w, y, 2, h, color);
    im[0].brightness = im[1].brightness = im[2].brightness = im[3].brightness = brightness;
    return 4;
}

static void MakeFieldSelectLayout(void)
{
    int speedShown, chosen = PortFieldSelectChoice(&speedShown);
    int n = 0, field, i, edge = 0, w = PortCanvasWidth(), h = PortCanvasHeight();

    // The border, at the canvas's edges (FS_BORDER_IMAGES images).
    for (i = 0; i < 3; i++)
    {
        int t = sFieldBorder[i][1];
        uint32_t c = sFieldBorder[i][0];
        SetRect(&sFieldImages[n++], edge, edge, w - 2 * edge, t, c);
        SetRect(&sFieldImages[n++], edge, h - edge - t, w - 2 * edge, t, c);
        SetRect(&sFieldImages[n++], edge, edge, t, h - 2 * edge, c);
        SetRect(&sFieldImages[n++], w - edge - t, edge, t, h - 2 * edge, c);
        edge += t;
    }
    for (field = 0; field < MAIN_FIELD_COUNT && gPortTablePictures; field++)
    {
        int brightness = field == chosen ? 256 : FS_DIM;
        sFieldImages[n++] = (struct PortLayoutImage){
            .pixels = &gPortTablePictures[field * PORT_TABLE_WIDTH * PORT_TABLE_HEIGHT],
            .w = PORT_TABLE_WIDTH, .h = PORT_TABLE_HEIGHT,
            .dx = FS_PREVIEW_X(field), .dy = FS_PREVIEW_Y, .dw = FS_PREVIEW_W, .dh = FS_PREVIEW_H,
            .brightness = brightness,
        };
        n += SetFrame(&sFieldImages[n], FS_PREVIEW_X(field), FS_PREVIEW_Y, FS_PREVIEW_W, FS_PREVIEW_H, FS_ORANGE, brightness);
        sFieldPieces[1 + field].darken = field == chosen ? 0 : FS_DIM_EVY;
    }
    // A panel for the Ball Speed box, once it's shown.
    sFieldLayout.pieceCount = ARRAY_COUNT(sFieldPieces) - !speedShown;
    if (speedShown)
    {
        int x = (PORT_CANVAS_WIDTH - FS_SPEED_W) / 2 - 4;
        SetRect(&sFieldImages[n++], x, FS_SPEED_Y - 4, FS_SPEED_W + 8, FS_SPEED_H + 8, FS_ORANGE);
        SetRect(&sFieldImages[n++], x + 2, FS_SPEED_Y - 2, FS_SPEED_W + 4, FS_SPEED_H + 4, FS_DARK);
    }
    // The GBA draws the box at x 36 (Ruby) or 140 (Sapphire), y 72.
    sFieldPieces[ARRAY_COUNT(sFieldPieces) - 1].sx = chosen == FIELD_RUBY ? 24 : 128;
    sFieldLayout.imageCount = n;
}

// For the mouse (mouse.c): where a table picture (0 Ruby, 1 Sapphire) or
// the Ball Speed box (2) is on the canvas.
void PortFieldSelectRect(int which, int *x, int *y, int *w, int *h)
{
    int16_t px, py;

    if (which < MAIN_FIELD_COUNT)
    {
        px = FS_PREVIEW_X(which);
        py = FS_PREVIEW_Y;
        *w = FS_PREVIEW_W;
        *h = FS_PREVIEW_H;
    }
    else
    {
        px = (PORT_CANVAS_WIDTH - FS_SPEED_W) / 2;
        py = FS_SPEED_Y;
        *w = FS_SPEED_W;
        *h = FS_SPEED_H;
    }
    sSplits = sFieldSplits;  // as Spread() placed them this frame
    sSplitCount = ARRAY_COUNT(sFieldSplits);
    MovePoint(&px, &py);
    *x = px;
    *y = py;
}

// High scores: the game keeps both lists side by side on 512-pixel-wide
// backgrounds (BG2 text, BG3 striped backdrop; Ruby at map x 0, Sapphire at
// 240) and scrolls between them. Here both are shown at once, Ruby above
// Sapphire, each over its own backdrop; the game's scrolling is ignored.
// They fill the canvas: the rows are spread
// apart by repeating two lines of the striped band under the 1st place row
// and of each separator between rows, so the stripes run on. Dialogs
// (sprites) stay in the middle. The footer (PRESS START TRANS: link
// transmission) isn't shown: the port has no link cable.
// The backdrop (BG3) has the game's Poké Ball watermark drawn into its
// stripes, which repeating lines would chop into steps (it came out
// mangled), so it's redrawn as a picture per list (ScoreBackdrop): the
// stripes spread like the text, the ball scaled evenly over them.
// Each list gets a bar above and below it in
// its side frame's colors, closing the frame: the top like the left side
// (grey), the bottom like the right side (a black line inside the grey).
#define HS_LIST_H 150  // header and 8 rows; the lines below them are the footer
#define HS_PANEL_GAP 14  // the bars and 6 lines between them
#define HS_BAR_H 4  // the side frame's width
#define HS_BAR_INNER 2  // its black line on the right
#define HS_MARGIN 8
#define HS_TOUCH_TOP 40  // touch screens: room for the BACK button
#define HS_MAX_GAP 24

// Where the list's lines are repeated: under the 1st place row, and in each
// separator between the other rows (two lines each, one stripe pair).
static const u8 sScoreGaps[] = {40, 54, 70, 86, 102, 118, 134};

static int sScoreGap;        // extra lines at each gap
static int sScoreTop[2];     // canvas y of each list

// One list's pieces: its lines in order, the gaps stretched.
static int ScoreListPieces(struct PortLayoutPiece *out, int x, int y, u8 layers, int fillUnder, int bgX)
{
    int n = 0, i, from = 0;

    for (i = 0; i <= (int)ARRAY_COUNT(sScoreGaps); i++)
    {
        int to = i < (int)ARRAY_COUNT(sScoreGaps) ? sScoreGaps[i] : HS_LIST_H;
        out[n] = (struct PortLayoutPiece){0, from, 240, to - from, x, y, layers, fillUnder, FALSE, bgX};
        y += to - from;
        n++;
        if (i == (int)ARRAY_COUNT(sScoreGaps))
            break;
        out[n] = (struct PortLayoutPiece){0, to, 240, 2, x, y, layers, fillUnder, FALSE, bgX};
        out[n].dh = 2 + sScoreGap;
        y += 2 + sScoreGap;
        n++;
        from = to + 2;
    }
    return n;
}

// The GBA line shown on line y of a spread list (as ScoreListPieces lays
// them out).
static int ScoreSourceLine(int y)
{
    int i, from = 0;

    for (i = 0; i < (int)ARRAY_COUNT(sScoreGaps); i++)
    {
        int to = sScoreGaps[i];

        if (y < to - from)
            return from + y;
        y -= to - from;
        if (y < 2 + sScoreGap)
            return to + (y & 1);
        y -= 2 + sScoreGap;
        from = to + 2;
    }
    return from + y;
}

// The watermark (measured on the GBA screen): a ball centered at
// (BALL_CX, BALL_CY), drawn as a darker shade of the stripes under it (with
// soft edges, and its ring left undarkened) in columns BALL_X0..BALL_X1-1;
// the rest of each line is one color across, apart from the frame. The
// backdrop ends at line 160, cutting the ball's bottom off; the spread ball
// is placed so that cut stays below the list.
#define BALL_CX 185
#define BALL_X0 100
#define BALL_X1 235  // the frame from here on
#define BALL_PLAIN_X 50  // a column without the ball
#define BALL_MAX_SCALE 1.5f
#define HS_MAX_LIST_H (HS_LIST_H + HS_MAX_GAP * ARRAY_COUNT(sScoreGaps))

static u32 Mix(u32 a, u32 b, int t)  // t of 256 from a to b
{
    u32 out = 0xFF000000;
    int k;

    for (k = 0; k < 24; k += 8)
    {
        int ca = (a >> k) & 0xFF, cb = (b >> k) & 0xFF;
        out |= (u32)(ca + (cb - ca) * t / 256) << k;
    }
    return out;
}

static int ColorDistance(u32 a, u32 b)
{
    int k, d = 0;

    for (k = 0; k < 24; k += 8)
    {
        int c = (int)((a >> k) & 0xFF) - (int)((b >> k) & 0xFF);
        d += c * c;
    }
    return d;
}

// Makes list `list`'s backdrop picture (240 x listH) from the game's BG3.
static void ScoreBackdrop(int list, int listH, uint32_t *out)
{
    static uint32_t src[2][PORT_SCREEN_HEIGHT * PORT_SCREEN_WIDTH], last[2][PORT_SCREEN_HEIGHT * PORT_SCREEN_WIDTH];
    static u16 shade[2][PORT_SCREEN_HEIGHT * PORT_SCREEN_WIDTH];  // how dark the ball makes each pixel (of 256)
    static int lastH[2];
    // Each stripe color and its shade under the ball (the most common one).
    static u32 stripe[2][64], dark[2][64];
    static int stripes[2];
    const uint32_t *s = src[list];
    float scale = (float)listH / HS_LIST_H;
    int x, y, i;

    PortCaptureBgMap(3, list * 240, 240, PORT_SCREEN_HEIGHT, src[list]);
    if (listH == lastH[list] && memcmp(src[list], last[list], sizeof(src[list])) == 0)
        return;  // as made last time
    memcpy(last[list], src[list], sizeof(src[list]));
    lastH[list] = listH;

    stripes[list] = 0;
    {
        static u32 seen[64][8];
        static int seenCount[64][8];

        memset(seenCount, 0, sizeof(seenCount));
        for (y = 0; y < PORT_SCREEN_HEIGHT; y++)
        {
            u32 plain = s[y * 240 + BALL_PLAIN_X];

            for (i = 0; i < stripes[list] && stripe[list][i] != plain; i++)
                ;
            if (i == stripes[list])
            {
                if (i == 64)
                    continue;
                stripe[list][stripes[list]++] = plain;
                memset(seen[i], 0, sizeof(seen[i]));
            }
            for (x = BALL_X0; x < BALL_X1; x++)
            {
                u32 c = s[y * 240 + x];
                int k;

                if (c == plain)
                    continue;
                for (k = 0; k < 8 && seenCount[i][k] && seen[i][k] != c; k++)
                    ;
                if (k < 8)
                    seen[i][k] = c, seenCount[i][k]++;
            }
        }
        for (i = 0; i < stripes[list]; i++)
        {
            int k, best = 0;

            dark[list][i] = stripe[list][i];
            for (k = 0; k < 8; k++)
                if (seenCount[i][k] > best)
                    best = seenCount[i][k], dark[list][i] = seen[i][k];
        }
    }
    // The shade of each pixel: how far it went from its line's stripe color
    // toward that color's shade (edge pixels go part of the way).
    for (y = 0; y < PORT_SCREEN_HEIGHT; y++)
    {
        u32 plain = s[y * 240 + BALL_PLAIN_X];

        for (i = 0; i < stripes[list] && stripe[list][i] != plain; i++)
            ;
        for (x = 0; x < 240; x++)
        {
            u32 c = s[y * 240 + x], d = i < stripes[list] ? dark[list][i] : plain;
            int full = ColorDistance(plain, d), t = 0, k;

            if (x >= BALL_X0 && x < BALL_X1 && c != plain && full)
            {
                for (k = 0; k < 24; k += 8)
                    t += ((int)((plain >> k) & 0xFF) - (int)((c >> k) & 0xFF))
                         * ((int)((plain >> k) & 0xFF) - (int)((d >> k) & 0xFF));
                t = t * 256 / full;
                t = t < 0 ? 0 : t > 256 ? 256 : t;
            }
            shade[list][y * 240 + x] = t;
        }
    }

    // The picture: each line's stripes from its GBA line (spread like the
    // text), the ball's shade from the GBA ball scaled evenly around its
    // center column, its line 150 (the list's end) at the picture's end.
    if (scale > BALL_MAX_SCALE)
        scale = BALL_MAX_SCALE;
    for (y = 0; y < listH; y++)
    {
        int line = ScoreSourceLine(y);
        u32 plain = s[line * 240 + BALL_PLAIN_X], d = plain;
        float by = HS_LIST_H - (listH - y) / scale;
        int y0 = (int)floorf(by), ty = (int)((by - y0) * 256);

        for (i = 0; i < stripes[list] && stripe[list][i] != plain; i++)
            ;
        if (i < stripes[list])
            d = dark[list][i];
        else
        {
            // A color the ball never covers on the GBA: the closest one's
            // shade, moved by the difference.
            int k, best = -1, bestD = 0;

            for (k = 0; k < stripes[list]; k++)
                if (best < 0 || ColorDistance(plain, stripe[list][k]) < bestD)
                    best = k, bestD = ColorDistance(plain, stripe[list][k]);
            if (best >= 0)
            {
                d = 0xFF000000;
                for (k = 0; k < 24; k += 8)
                {
                    int c = (int)((plain >> k) & 0xFF) + (int)((dark[list][best] >> k) & 0xFF)
                            - (int)((stripe[list][best] >> k) & 0xFF);
                    d |= (u32)(c < 0 ? 0 : c > 255 ? 255 : c) << k;
                }
            }
        }
        for (x = 0; x < 240; x++)
        {
            float bx = BALL_CX + (x - BALL_CX) / scale;
            int x0 = (int)floorf(bx), tx = (int)((bx - x0) * 256), t = 0, j;

            if (x < BALL_X0 - 40 || x >= BALL_X1)
            {
                out[y * 240 + x] = s[line * 240 + x];  // the frame, and the stripes left of the ball
                continue;
            }
            // Bilinear: the four GBA pixels around (bx, by).
            for (j = 0; j < 4; j++)
            {
                int sx = x0 + (j & 1), sy = y0 + (j >> 1);
                int w = ((j & 1) ? tx : 256 - tx) * ((j >> 1) ? ty : 256 - ty) >> 8;

                if (sx >= BALL_X0 && sx < BALL_X1 && sy >= 0 && sy < PORT_SCREEN_HEIGHT)
                    t += shade[list][sy * 240 + sx] * w;
            }
            out[y * 240 + x] = Mix(plain, d, t >> 8);
        }
    }
}

int PortHighScoreNameEntryRow(void);  // high_scores.c

static const struct PortLayout *ScoreLayout(int entry)
{
    static struct PortLayoutPiece pieces[4 * (2 * ARRAY_COUNT(sScoreGaps) + 1) + 2];
    static struct PortLayoutImage images[1 + 2 + 2 * 3];
    struct PortLayoutImage *backdrops = &images[1];
    static uint32_t backdropPixels[2][240 * HS_MAX_LIST_H];
    static struct PortLayout layout;
    int w = PortCanvasWidth(), h = PortCanvasHeight(), top = PortTouchEnabled() ? HS_TOUCH_TOP : HS_MARGIN;
    int x = (w - 240) / 2, natural = 2 * HS_LIST_H + HS_PANEL_GAP;
    int gaps = 2 * ARRAY_COUNT(sScoreGaps), listH, n = 0, i;

    sScoreGap = (h - top - HS_MARGIN - natural) / gaps & ~1;  // even: whole stripe pairs
    if (sScoreGap < 0)
        sScoreGap = 0;
    if (sScoreGap > HS_MAX_GAP)
        sScoreGap = HS_MAX_GAP;
    listH = HS_LIST_H + sScoreGap * gaps / 2;
    top += (h - top - HS_MARGIN - (natural + sScoreGap * gaps)) / 2;  // what's left: above and below
    sScoreTop[0] = top;
    sScoreTop[1] = top + listH + HS_PANEL_GAP;

    // Text over the redrawn backdrops.
    n += ScoreListPieces(&pieces[n], x, sScoreTop[0], PORT_LAYER_BG(2), TRUE, 0 + 1);
    n += ScoreListPieces(&pieces[n], x, sScoreTop[1], PORT_LAYER_BG(2), TRUE, 240 + 1);
    for (i = 0; i < 2; i++)
    {
        ScoreBackdrop(i, listH, backdropPixels[i]);
        backdrops[i] = (struct PortLayoutImage){backdropPixels[i], 0, 240, listH, x, sScoreTop[i], 0, 0, 256};
    }
    {
        // Sapphire's frame grey is a touch purple (0x4A424A); with the grey
        // all around (Ruby's), it's made Ruby's so the two don't clash.
        u32 grey = backdropPixels[0][HS_BAR_H * 240], other = backdropPixels[1][HS_BAR_H * 240];

        if (other != grey)
            for (i = 0; i < 240 * listH; i++)
                if (backdropPixels[1][i] == other)
                    backdropPixels[1][i] = grey;
    }
    for (i = 0; i < 2; i++)
    {
        const uint32_t *row = &backdropPixels[i][HS_BAR_H * 240];  // a line of plain frame
        u32 grey = row[0], black = row[240 - HS_BAR_H];
        struct PortLayoutImage *bar = &backdrops[2 + 3 * i];
        int bottom = sScoreTop[i] + listH;

        bar[0] = (struct PortLayoutImage){NULL, grey, 240, HS_BAR_H, x, sScoreTop[i] - HS_BAR_H, 0, 0, 256};
        bar[1] = (struct PortLayoutImage){NULL, grey, 240, HS_BAR_H, x, bottom, 0, 0, 256};
        // Its black line runs from the left side's grey to the right side's black.
        bar[2] = (struct PortLayoutImage){NULL, black, 240 - HS_BAR_H - HS_BAR_INNER, HS_BAR_INNER,
                                          x + HS_BAR_H, bottom, 0, 0, 256};
    }
    if (entry == 0 || entry == 1)
    {
        // Entering a name: the game shows that list (scrolled to it); its
        // cursor arrows (sprites) go over it, instead of the dialogs. They
        // reach into the separators above and below the row, so they're
        // drawn unstretched, moved down with the row (through the spread
        // pieces their lines in the separators were repeated).
        int row = PortHighScoreNameEntryRow(), from, to, shift;

        if (row < 0 || row > (int)ARRAY_COUNT(sScoreGaps))
            row = 0;
        from = row == 0 ? 0 : sScoreGaps[row - 1] + 2;
        to = row < (int)ARRAY_COUNT(sScoreGaps) ? sScoreGaps[row] : HS_LIST_H;
        shift = row * sScoreGap;  // each separator above the row is sScoreGap lines taller
        from = from - 16 < 0 ? 0 : from - 16;
        to = to + 16 > HS_LIST_H ? HS_LIST_H : to + 16;
        pieces[n++] = (struct PortLayoutPiece){0, from, 240, to - from, x, sScoreTop[entry] + from + shift,
                                               PORT_LAYER_OBJ, TRUE};
    }
    else
    {
        // Dialogs.
        pieces[n++] = (struct PortLayoutPiece){0, 0, 240, HS_LIST_H, x, (h - 160) / 2, PORT_LAYER_OBJ, TRUE};
    }
    // Around the lists: the frames' dark grey,
    // instead of each list's stripes.
    images[0] = (struct PortLayoutImage){NULL, backdropPixels[0][HS_BAR_H * 240], w, h, 0, 0, 0, 0, 256};
    layout = (struct PortLayout){.fillBg = -1, .imageCount = ARRAY_COUNT(images), .images = images,
                                 .pieceCount = n, .pieces = pieces};
    return &layout;
}
int PortHighScoreNameEntryField(void);  // high_scores.c

// Pokédex (filling the screen top to bottom, portraits in the list): header and info panel at the top,
// then as many list rows as fit, each DEX_ROW_H tall: its text (the game's
// 16-line row: rows 1-5 from lines 80-159, columns 0-159, the next ones
// from the pictures of the next pages' text that pokedex.c keeps,
// gPortDexPages) and, once seen, its Pokémon's whole portrait
// (PortDexPortrait) on its right, with a little of the backdrop between
// portraits, all over one backdrop: the stripes repeated, the frame's edges
// stretched to the bottom, the game's backdrop with its Poké Ball watermark
// (gPortDexBack) at the bottom, under Seen/Own (the right part of lines
// 120-141), which sit below the rows. PRESS START TRANS is hidden, as the
// port has no link cable. One long scroll bar
// runs beside all rows, made of the game's (gPortDexBar: cap, track
// repeated, cap), its knob placed along it; the cursor is drawn at its row
// (both are sprites that pokedex.c puts under the screen). The
// description drawer is drawn from pokedex.c's pictures, tall enough for all
// its text (its height follows the game's slide), over pictures of the list
// taken as it opened (the game erases and restores the rows under it). While
// a pop-up is open (full width), the GBA screen is shown whole instead
// (minus its own short scroll bar and backdrop, BG3), with the other pages
// below it, 16 lines a row and no portraits; the knob then goes into the GBA screen where it falls inside it,
// so the pop-up covers it as on the GBA.
#define DEX_ROW_H 36       // a row: room for a portrait and 4 lines between portraits
#define DEX_TEXT_Y 10      // the row's text (16 lines) and portrait (32), from its top
#define DEX_PORTRAIT_Y 2
#define DEX_SHADOW 2       // the portraits' drop shadow: this far down and right,
#define DEX_SHADOW_COLOR 0x70000000  // black, this see-through
#define DEX_LINE_H 16      // a row of the game's list
#define DEX_TOTALS_TOP 40  // Seen/Own: lines 40-61 of the list's 80
#define DEX_TOTALS_H 22
#define DEX_FOOTER 28      // from the last row to the bottom: Seen/Own, a margin under it
#define DEX_PORTRAIT_X 178 // between the names and the frame's right edge
#define DEX_TOUCH_TOP 40  // touch screens: room for the BACK button
#define DEX_FRAME_LEFT 5   // the frame's edges (BG3 columns)
#define DEX_FRAME_RIGHT 5
#define DEX_TEXT_LAYERS (PORT_LAYER_BG(1) | PORT_LAYER_OBJ)
// pokedex.c's states (gPokedexStateFuncs) in which the list is browsed:
// loading, list input, after scrolling; and the fade back to the title.
#define DEX_LIST_STATES ((1 << 0) | (1 << 1) | (1 << 2) | (1 << 12))
extern s16 gPokedexCursorOffset, gPokedexSelectedMon, gPokedexListEntryCount, gPokedexInfoWindowSlideStep, gPokedexListPosition;

static int DexTop(int h)
{
    return PortTouchEnabled() ? DEX_TOUCH_TOP : 0;
}

int PortDexListRows(void)
{
    int w, h, rows;

    PortMenuSize(&w, &h);
    rows = (h - DexTop(h) - 80 - DEX_FOOTER) / DEX_ROW_H;
    if (rows < 5)
        rows = 5;
    if (rows > PORT_DEX_MAX_ROWS)
        rows = PORT_DEX_MAX_ROWS;
    return rows;
}

// The canvas place of the list's top-left corner (the header is 80 lines above).
static void DexListPlace(int *x, int *y)
{
    *x = (PortCanvasWidth() - 240) / 2;
    *y = DexTop(PortCanvasHeight()) + 80;  // even: the stripes line up
}

// Where the Seen/Own lines go: at the bottom, 4 lines up.
static int DexTotalsY(void)
{
    return PortCanvasHeight() - 4 - DEX_TOTALS_H;
}

static int DexWhole(void)
{
    return !((DEX_LIST_STATES >> gMain.subState) & 1);
}

// pokedex.c's states 3 (drawer sliding open), 4 (sliding shut), 5 (open).
static int DexDrawerOpen(void)
{
    return gMain.subState >= 3 && gMain.subState <= 5;
}

static int DexTrackH(void)
{
    return PortDexListRows() * DEX_ROW_H - 2 * PORT_DEX_BAR_TOP - 2;
}

// How many lines of the drawer show under the list's top: the game's drawer
// grows down one tile row per step from row 9 to line 144; ours goes the
// same part of the way to its full height.
static int DexDrawerShown(void)
{
    int end;

    switch (gMain.subState)
    {
    case 3:
        end = (gPokedexInfoWindowSlideStep + 10) * 8;
        break;
    case 4:
        end = gPokedexInfoWindowSlideStep < 1 ? 144 : (19 - gPokedexInfoWindowSlideStep) * 8;
        break;
    default:
        return gPortDexDrawerH;
    }
    return (end - 80) * gPortDexDrawerH / 64;
}

// Adds a piece without its lines above canvas line `top`.
static void PieceBelow(struct PortLayoutPiece *pieces, int *n, struct PortLayoutPiece piece, int top)
{
    int skip = top - piece.dy;

    if (skip >= piece.h)
        return;
    if (skip > 0)
    {
        piece.sy += skip;
        piece.dy += skip;
        piece.h -= skip;
    }
    pieces[(*n)++] = piece;
}

// The knob's top, from the list's top (GBA line 80).
static int DexKnobOffset(void)
{
    int last = gPokedexListEntryCount > 1 ? gPokedexListEntryCount - 1 : 1;

    return PORT_DEX_BAR_TOP + 1 + (DexTrackH() - 3) * gPokedexSelectedMon / last;
}

// The GBA line pokedex.c puts the knob on: inside the GBA screen at its place
// on the long bar while the screen is shown whole and it fits there, else
// out of sight (PORT_DEX_KNOB_LINE), for DexLayout to draw.
int PortDexKnobLine(void)
{
    int line = 80 + DexKnobOffset();

    return DexWhole() && !DexDrawerOpen() && line + 8 <= PORT_SCREEN_HEIGHT ? line : PORT_DEX_KNOB_LINE;
}

void PortDexRowRect(int row, int *x, int *y, int *w, int *h)
{
    DexListPlace(x, y);
    *y += row * DEX_ROW_H;
    *w = DEX_PORTRAIT_X + PORT_DEX_PORTRAIT_W;
    *h = DEX_ROW_H;
}

// The long scroll bar, for the mouse: where it can be grabbed (canvas), and
// the entry whose knob is centered on canvas line y.
void PortDexBarRect(int *x, int *y, int *w, int *h)
{
    DexListPlace(x, y);
    *x += PORT_DEX_BAR_X - 5;
    *w = PORT_DEX_BAR_W + 7;
    *h = PortDexListRows() * DEX_ROW_H;
}

int PortDexBarItem(int canvasY)
{
    int x, y, last = gPokedexListEntryCount > 1 ? gPokedexListEntryCount - 1 : 1, span = DexTrackH() - 3, offset;

    DexListPlace(&x, &y);
    offset = canvasY - 4 - (y + PORT_DEX_BAR_TOP + 1);
    if (offset < 0)
        offset = 0;
    if (offset > span)
        offset = span;
    return (offset * last + span / 2) / span;
}

// pokedex.c moves the cursor under the screen, for DexLayout to draw at
// its row: always, except below row 5 only while the GBA screen is shown
// whole (a pop-up).
int PortDexCursorParked(void)
{
    return !(DexWhole() && !DexDrawerOpen()) || gPokedexCursorOffset >= 5;
}

static const struct PortLayout *DexLayout(void)
{
    static struct PortLayoutPiece pieces[16];
    static struct PortLayoutImage images[3 * PORT_DEX_MAX_ROWS + PORT_DEX_MAX_PAGES + 8];
    static struct PortLayoutTile tiles[1];
    static struct PortLayout layout;
    int rows = PortDexListRows(), x, y, n = 0, m = 0, page, row;
    int frameH = PortCanvasHeight(), totalsY = DexTotalsY();
    int whole = DexWhole() && !DexDrawerOpen(), drawer = DexDrawerOpen() ? DexDrawerShown() : 0, trackY, trackH;

    DexListPlace(&x, &y);
    // The frame's edges down the list (before the GBA screen's pieces, which
    // go over them where they meet).
    frameH -= y;
    pieces[n] = (struct PortLayoutPiece){0, 80, DEX_FRAME_LEFT, 1, x, y, PORT_LAYER_BG(3), FALSE};
    pieces[n++].dh = frameH;
    pieces[n] = (struct PortLayoutPiece){240 - DEX_FRAME_RIGHT, 80, DEX_FRAME_RIGHT, 1, x + 240 - DEX_FRAME_RIGHT, y,
                                         PORT_LAYER_BG(3), FALSE};
    pieces[n++].dh = frameH;
    pieces[n++] = (struct PortLayoutPiece){0, 0, 240, 80, x, y - 80, PORT_LAYER_ALL, FALSE};
    if (whole)
    {
        pieces[n++] = (struct PortLayoutPiece){0, 80, 240, 80, x, y, PORT_LAYER_ALL & ~PORT_LAYER_BG(3), TRUE};
        for (page = 1; page * 5 < rows && page < PORT_DEX_MAX_PAGES; page++)
        {
            int h = (rows - page * 5) * DEX_LINE_H;

            images[m++] = (struct PortLayoutImage){gPortDexPages[page], 0, PORT_DEX_PAGE_W,
                                                   h < PORT_DEX_PAGE_H ? h : PORT_DEX_PAGE_H,
                                                   x, y + page * PORT_DEX_PAGE_H, 0, 0, 256};
        }
        if (gPokedexCursorOffset >= 5)
            pieces[n++] = (struct PortLayoutPiece){0, PORT_DEX_CURSOR_LINE, PORT_DEX_PAGE_W, 24,
                                                   x, y + gPokedexCursorOffset * DEX_LINE_H, PORT_LAYER_OBJ, TRUE};
    }
    else
    {
        // Each row's text: the game's own rows 1-5 (while the drawer is
        // open, the picture of them taken as it opened), then the pictures.
        for (row = 0; row < rows; row++)
        {
            int dy = y + row * DEX_ROW_H + DEX_TEXT_Y;

            if (row < 5 && !DexDrawerOpen())
                pieces[n++] = (struct PortLayoutPiece){0, 80 + row * DEX_LINE_H, PORT_DEX_PAGE_W, DEX_LINE_H,
                                                       x, dy, PORT_LAYER_BG(1), TRUE};
            else if (row / 5 < PORT_DEX_MAX_PAGES)
                images[m++] = (struct PortLayoutImage){&gPortDexPages[row / 5][row % 5 * DEX_LINE_H * PORT_DEX_PAGE_W], 0,
                                                       PORT_DEX_PAGE_W, DEX_LINE_H, x, dy, 0, 0, 256};
        }
        for (row = 0; row < rows; row++)  // portraits, each over its drop shadow
        {
            const uint32_t *portrait = PortDexPortrait(gPokedexListPosition + row);

            if (!portrait)
                continue;
            images[m++] = (struct PortLayoutImage){NULL, DEX_SHADOW_COLOR, PORT_DEX_PORTRAIT_W, PORT_DEX_PORTRAIT_H,
                                                   x + DEX_PORTRAIT_X + DEX_SHADOW, y + row * DEX_ROW_H + DEX_PORTRAIT_Y + DEX_SHADOW,
                                                   0, 0, 256};
            images[m++] = (struct PortLayoutImage){portrait, 0, PORT_DEX_PORTRAIT_W, PORT_DEX_PORTRAIT_H,
                                                       x + DEX_PORTRAIT_X, y + row * DEX_ROW_H + DEX_PORTRAIT_Y, 0, 0, 256};
        }
        // Seen/Own (text, sprites) over the backdrop.
        if (!DexDrawerOpen())
            pieces[n++] = (struct PortLayoutPiece){PORT_DEX_PAGE_W, 80 + DEX_TOTALS_TOP, 240 - PORT_DEX_PAGE_W, DEX_TOTALS_H,
                                                   x + PORT_DEX_PAGE_W, totalsY, DEX_TEXT_LAYERS, TRUE};
        PieceBelow(pieces, &n, (struct PortLayoutPiece){0, PORT_DEX_CURSOR_LINE, PORT_DEX_PAGE_W, 24,
                                                        x, y + gPokedexCursorOffset * DEX_ROW_H + DEX_TEXT_Y,
                                                        PORT_LAYER_OBJ, TRUE},
                   y + drawer);
    }
    trackY = y + PORT_DEX_BAR_TOP + 1;
    trackH = DexTrackH();
    if (PortDexKnobLine() == PORT_DEX_KNOB_LINE)
        PieceBelow(pieces, &n, (struct PortLayoutPiece){8, PORT_DEX_KNOB_LINE, 16, 8, x + 8, y + DexKnobOffset(),
                                                        PORT_LAYER_OBJ, TRUE},
                   y + drawer);

    // Under everything drawn so far: the backdrop, and outside the frame
    // its outer light grey, as the GBA's
    // frame meets the screen's edges.
    memmove(&images[3], &images[0], sizeof(images[0]) * m);
    m += 3;
    images[0] = (struct PortLayoutImage){gPortDexBack, 0, 240, PORT_DEX_PAGE_H, x, PortCanvasHeight() - 80, 0, 0, 256};
    images[1] = (struct PortLayoutImage){NULL, gPortDexBack[0], x, PortCanvasHeight(), 0, 0, 0, 0, 256};
    images[2] = (struct PortLayoutImage){NULL, gPortDexBack[239], PortCanvasWidth() - x - 240, PortCanvasHeight(),
                                         x + 240, 0, 0, 0, 256};
    images[m++] = (struct PortLayoutImage){&gPortDexBar[0], 0, PORT_DEX_BAR_W, 1,
                                           x + PORT_DEX_BAR_X, trackY - 1, 0, 0, 256};
    images[m++] = (struct PortLayoutImage){&gPortDexBar[PORT_DEX_BAR_W], 0, PORT_DEX_BAR_W, 1,
                                           x + PORT_DEX_BAR_X, trackY, PORT_DEX_BAR_W, trackH, 256};
    images[m++] = (struct PortLayoutImage){&gPortDexBar[(PORT_DEX_BAR_H - 1) * PORT_DEX_BAR_W], 0, PORT_DEX_BAR_W, 1,
                                           x + PORT_DEX_BAR_X, trackY + trackH, 0, 0, 256};
    if (DexDrawerOpen())
    {
        // The list as the drawer found it, then the drawer: with its text
        // once open and while it slides shut (cut off from the bottom up,
        // as the game does), empty while it slides open (its middle, then
        // its last lines).
        const uint32_t *pic = gMain.subState == 4 ? gPortDexDrawer : gPortDexDrawerEmpty;

        images[m++] = (struct PortLayoutImage){&gPortDexOpenTotals[DEX_TOTALS_TOP * (240 - PORT_DEX_PAGE_W)], 0,
                                               240 - PORT_DEX_PAGE_W, DEX_TOTALS_H, x + PORT_DEX_PAGE_W, totalsY, 0, 0, 256};
        if (gMain.subState == 5)
            images[m++] = (struct PortLayoutImage){gPortDexDrawer, 0, 240, gPortDexDrawerH, x, y, 0, 0, 256};
        else if (drawer > 0)
        {
            int end = drawer < PORT_DEX_DRAWER_END ? drawer : PORT_DEX_DRAWER_END;

            if (drawer > end)
                images[m++] = (struct PortLayoutImage){pic, 0, 240, drawer - end, x, y, 0, 0, 256};
            images[m++] = (struct PortLayoutImage){&pic[240 * (gPortDexDrawerH - end)], 0, 240, end,
                                                   x, y + drawer - end, 0, 0, 256};
        }
    }
    tiles[0] = (struct PortLayoutTile){3, 24, 88, 8, 8, 0, 0, PortCanvasWidth(), PortCanvasHeight()};
    layout = (struct PortLayout){.fillBg = -1, .tileCount = 1, .tiles = tiles, .imageCount = m, .images = images,
                                 .pieceCount = n, .pieces = pieces};
    return &layout;
}

// Bonus stages: the main table the ball came from stays behind, dimmed, and
// the stage (the whole GBA screen) is drawn over it, low enough that its
// flippers are near the main table's flippers.
#define BONUS_X 8
#define BONUS_Y 254
#define BONUS_DIM 77  // table brightness, out of 256
#define BONUS_FRAME 0xFFE6E6E6

static uint32_t sBonusImage[PORT_CANVAS_WIDTH * PORT_CANVAS_HEIGHT];

static const struct PortLayoutPiece sBonusPieces[] = {
    {0, 0, PORT_SCREEN_WIDTH, PORT_SCREEN_HEIGHT, BONUS_X, BONUS_Y, PORT_LAYER_ALL, FALSE},
};

static const struct PortLayout sBonusLayout = {
    .fillBg = -1, .image = sBonusImage,
    .pieceCount = ARRAY_COUNT(sBonusPieces), .pieces = sBonusPieces,
};

// The table behind a bonus stage: the last frame of the table the ball came
// from, or (when the stage was picked from the Bonus Field Select menu) a
// picture of the table that stage belongs to.
static const uint32_t *BonusTable(void)
{
    int lastField, field = gMain.tempField;
    const uint32_t *last = PortLastTable(&lastField);

    if (field < MAIN_FIELD_COUNT && last && lastField == field)
        return last;
    if (field >= MAIN_FIELD_COUNT)
        field = (gMain.selectedField == FIELD_KYOGRE || gMain.selectedField == FIELD_SPHEAL) ? FIELD_SAPPHIRE : FIELD_RUBY;
    if (!gPortTablePictures)
        return NULL;
    return &gPortTablePictures[field * PORT_TABLE_WIDTH * PORT_TABLE_HEIGHT];
}

static void MakeBonusImage(void)
{
    const uint32_t *table = BonusTable();
    int x, y, k;

    for (x = 0; x < PORT_CANVAS_WIDTH * PORT_CANVAS_HEIGHT; x++)
        sBonusImage[x] = 0xFF000000;
    for (y = 0; table && y < PORT_TABLE_HEIGHT; y++)
    {
        for (x = 0; x < PORT_TABLE_WIDTH; x++)
        {
            uint32_t c = table[y * PORT_TABLE_WIDTH + x], out = 0xFF000000;
            for (k = 0; k < 24; k += 8)
                out |= ((((c >> k) & 0xFF) * BONUS_DIM) >> 8) << k;
            sBonusImage[(PORT_TABLE_Y + y) * PORT_CANVAS_WIDTH + PORT_TABLE_X + x] = out;
        }
    }
    // A 2-pixel frame around the stage.
    for (y = BONUS_Y - 2; y < BONUS_Y + PORT_SCREEN_HEIGHT + 2; y++)
    {
        for (x = BONUS_X - 2; x < BONUS_X + PORT_SCREEN_WIDTH + 2; x++)
            sBonusImage[y * PORT_CANVAS_WIDTH + x] = BONUS_FRAME;
    }
}

// The window's bars beside the picture take the picture's left edge color
// on these screens (the Pokédex's grey sides run on to the window's
// borders); elsewhere they're black.
int PortWindowBarsFromEdge(void)
{
    return gPortStaticView && gMain.mainState == STATE_POKEDEX;
}

const struct PortLayout *PortScreenLayout(void)
{
    static const struct PortLayout *previous;
    const struct PortLayout *layout = NULL;

    switch (gMain.mainState)
    {
    case STATE_TITLE:
        layout = Spread(TitleLayout(), sTitleSplits, ARRAY_COUNT(sTitleSplits), 0);
        break;
    case STATE_POKEDEX:
        layout = DexLayout();
        break;
    case STATE_SCORES_MAIN:
    case STATE_SCORES_IDLE:
    {
        layout = ScoreLayout(PortHighScoreNameEntryField());
        break;
    }
    case STATE_FIELD_SELECT:
        MakeFieldSelectLayout();
        layout = Spread(&sFieldLayout, sFieldSplits, ARRAY_COUNT(sFieldSplits), FS_BORDER_IMAGES);
        break;
    case STATE_GAME_MAIN:
    case STATE_GAME_IDLE:
        if (gMain.selectedField >= FIELD_BONUS_START)
            layout = &sBonusLayout;
        break;
    }
    if (layout == &sBonusLayout && previous != &sBonusLayout)
        MakeBonusImage();
    previous = layout;
    return layout;
}
