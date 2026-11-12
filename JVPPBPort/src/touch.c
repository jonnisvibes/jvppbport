// JVPPBPort: the touch screen (Android). An "arcade cabinet" design:
// - While a game is on, the screen is the game's canvas (the table, 256x448)
//   at the top, centered, with a cabinet strip under it holding two big
//   round chrome flipper buttons and a pause button between them. Menus and
//   other screens show only the canvas, centered on black (the cabinet
//   only in a game). The strip
//   takes what the screen's shape leaves under the table (at least
//   MIN_STRIP); a screen wider than that (tablets) gets cabinet at the sides
//   too, with chrome rails along the table. Red for Ruby, blue for Sapphire.
// - Flippers: a finger on the strip's left half holds the left flipper, on
//   the right half the right one (which also launches the ball, as on the
//   GBA). The middle button is Esc: pause while playing, back in menus.
// - Tilt: a swipe that starts on the table, left, right or up, at least
//   SWIPE_MIN pixels long. One tilt per swipe. A tap on the table while
//   playing is a click (A: says yes to the game's prompts, e.g. at the boss
//   hole; the pause button says no there).
// - Launch (BUTTONS): a finger on Spoink (the launcher, bottom right of
//   the main tables) holds LAUNCH while it stays down, like the right
//   flipper.
// - Options -> PINBALL -> TOUCH < BUTTONS / PLAIN / ZONES / OFF >. PLAIN:
//   BUTTONS without the flipper pictures on the round buttons. ZONES: no
//   cabinet; the table sits in the middle of a black screen with a small
//   pause button in the top right corner (BACK in the Mart). A finger
//   anywhere else on the screen's left half holds the left flipper, on the
//   right half the right one (which launches), and a swipe from it tilts as
//   well. In the Mart / Evolution Select and at the game's yes/no prompts
//   fingers work the panel and taps are clicks, as elsewhere. OFF (for playing with a
//   controller): no flipper or pause buttons and no tilt swipes; the table
//   sits in the middle of the cabinet. Taps still work the menus (and say
//   yes to prompts). Kept in jvppbport.ini [Controls] TouchButtons
//   (0 off, 1 buttons, 2 zones, 3 plain).
// - The Mart and Evolution Select (their own arrows are small targets on a
//   phone): big round arrow buttons lie over the panel's arrows (ShopZoneAt,
//   ShopOverlay), the flipper buttons (still drawn as flippers) press Left / Right
//   (on the GBA the left flipper is the D-pad too), and the Mart has a BACK
//   button under its panel in every TOUCH mode. A finger on the picture
//   between the arrows picks it (mouse.c). Evolution Select has no back on
//   the GBA, so its middle button pauses.
// - Menus where B goes back (Options, Pokédex, Field Select...) get a BACK
//   button in the top left corner (with Android's gesture navigation,
//   Back is an edge swipe and hard to hit).
// - Everywhere else (menus, pause, Mart, Evolution Select, the setup screen)
//   a finger works like the mouse (mouse.c): touch = point there, lift =
//   click there. A swipe instead presses the D-pad that way (one press, then
//   one more every SWIPE_STEP pixels), as wheel notches: it changes letters
//   when entering a name, picks the evolution... (the Pokédex list is
//   dragged instead: a finger there works as a held mouse button)
//
// The canvas pixels here are "screen pixels": the whole screen, of which the
// game's canvas is a part (at CanvasX(), CanvasY()). Fingers come in as fractions of
// the window. JVPPBPORT_TOUCH=WxH shows this layout on a PC too, for a
// window of that shape (screenshots and looks only; the mouse stays a mouse).
// Test mode: JVPPBPORT_FINGER=4100:down:128:200,4102:move:170:200,4104:up:170:200
// moves one finger at those frames (screen pixels); JVPPBPORT_MOUSE_TRACE=1
// prints the tilts it makes.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "global.h"
#include "main.h"
#include "constants/fields.h"
#include "constants/pinball_inputs.h"
#include "jvppbport.h"

#define MIN_STRIP 96       // the cabinet strip is at least this tall
#define MAX_SCREEN_W 640
#define MAX_SCREEN_H 1280
#define SWIPE_MIN 20       // screen pixels a finger must move to tilt (or swipe in a menu)
#define SWIPE_STEP 24      // in a menu, one more D-pad press each time a swipe goes this much further
#define TILT_FRAMES 6      // a tilt holds its action this long, like a key tap
#define MAX_FINGERS 10

enum
{
    ZONE_NONE,
    ZONE_LEFT,    // left flipper
    ZONE_RIGHT,   // right flipper
    ZONE_PAUSE,   // the middle button
    ZONE_LAUNCH,  // Spoink, the launcher
    ZONE_SWIPE,   // on the table while playing: swipes tilt
    ZONE_MOUSE,   // would work like the mouse, but another finger already does
    ZONE_MENU,    // works like the mouse (only one finger at a time)
    ZONE_UP,      // Evolution Select's arrows over and under the picture
    ZONE_DOWN,
};

static struct
{
    SDL_FingerID id;
    int zone;
    float x0, y0;  // where it went down, in screen pixels
    float lastY;   // a drag of the Options screen: where it was last
    bool8 used;    // a swipe that already tilted; in a menu: a swipe, not a tap
    int8_t axis;   // a menu swipe: 0 across, 1 up/down
    int steps;     // a menu swipe: D-pad presses made so far
    bool8 held;    // a menu finger on the Pokédex list or its bar: a held mouse button
} sFingers[MAX_FINGERS];
static int sFingerCount;

static int sEnabled = -1;
static int sScreenW, sScreenH;  // the whole screen, in screen pixels
static int sWindowW, sWindowH;  // the window (or the shape being tried on a PC)
static int sGameX;              // the game's canvas in a game: (sGameX, 0)
static uint32_t sScreen[MAX_SCREEN_W * MAX_SCREEN_H];
static int sTiltAction = -1, sTiltFrames;
static bool8 sPausePressed;
static int sSapphire;           // the cabinet's color follows the last main table
static int sControls = -1;      // PORT_TOUCH_* (-1: not read yet)

// Layout of the strip (worked out by Layout()).
static int sStripY, sButtonR, sLeftX, sRightX, sButtonY, sPauseR;

#define MENU_BACK_X 12      // menus' BACK button (in from the screen's rounded corner)
#define MENU_BACK_Y 16
#define MENU_BACK_W 46
#define MENU_BACK_H 20
#define SPEED_R 17          // Field Select's Ball Speed arrows, beside its box
#define SPEED_GAP 5

int PortTouchEnabled(void)
{
    if (sEnabled < 0)
    {
#ifdef __ANDROID__
        sEnabled = 1;
#else
        sEnabled = getenv("JVPPBPORT_TOUCH") != NULL;
#endif
    }
    return sEnabled;
}

// Options -> TOUCH: PORT_TOUCH_BUTTONS (flipper and pause buttons, tilt
// swipes), PORT_TOUCH_ZONES (the screen's halves are the flippers) or
// PORT_TOUCH_OFF (0, so it can be used as a yes/no: on-screen controls).
int PortTouchControls(void)
{
    if (!PortTouchEnabled())
        return PORT_TOUCH_OFF;
    if (sControls < 0)
    {
        sControls = PortSettingGetInt("Controls", "TouchButtons", PORT_TOUCH_BUTTONS);
        if (sControls < PORT_TOUCH_OFF || sControls > PORT_TOUCH_PLAIN)
            sControls = PORT_TOUCH_BUTTONS;
    }
    return sControls;
}

void PortSetTouchControls(int mode)
{
    sControls = mode;
    PortSettingSetInt("Controls", "TouchButtons", sControls);
    PortSettingsSave();
    PortTouchReset();
}

// Whole-number scales for this window (Options -> SCREEN -> SCALE): at most
// what leaves the table and the cabinet strip on screen, at least what the
// picture buffers can hold. *max is below *min if none works.
void PortTouchScaleRange(int *min, int *max)
{
    int w = sWindowW > 0 ? sWindowW : PORT_CANVAS_WIDTH, h = sWindowH > 0 ? sWindowH : PORT_CANVAS_HEIGHT + MIN_STRIP;

    *max = w / PORT_CANVAS_WIDTH < h / (PORT_CANVAS_HEIGHT + MIN_STRIP) ? w / PORT_CANVAS_WIDTH : h / (PORT_CANVAS_HEIGHT + MIN_STRIP);
    for (*min = 1; w / *min > MAX_SCREEN_W || h / *min > MAX_SCREEN_H; ++*min)
        ;
}

// The whole-number scale in use, 0 for FILL (or if none works for this window).
static int LayoutScale(void)
{
    int min, max, n = PortScaleMode();

    PortTouchScaleRange(&min, &max);
    if (n == 0 || max < min)
        return 0;
    if (n == PORT_SCALE_SNAP)
        return max;
    return n < min ? min : n > max ? max : n;
}

static void Layout(int windowW, int windowH)
{
    int gameW = PORT_CANVAS_WIDTH, gameH = PORT_CANVAS_HEIGHT, n;  // the table's canvas

    if (windowW <= 0 || windowH <= 0)
        windowW = gameW, windowH = gameH + MIN_STRIP;
    sWindowW = windowW;
    sWindowH = windowH;
    n = LayoutScale();
    if (n)
    {
        // Whole-number scale: the screen is that many times smaller than
        // the window (the table doesn't grow to fill it; the cabinet does).
        sScreenW = windowW / n;
        sScreenH = windowH / n;
    }
    else
    {
        // The screen in canvas pixels, with the window's shape: as wide as the
        // table, unless that leaves less than MIN_STRIP under it.
        sScreenH = (int)((long)gameW * windowH / windowW);
        if (sScreenH < gameH + MIN_STRIP)
            sScreenH = gameH + MIN_STRIP;
        sScreenW = (int)((long)sScreenH * windowW / windowH);
    }
    if (sScreenW < gameW)
        sScreenW = gameW;
    if (sScreenW > MAX_SCREEN_W)
        sScreenW = MAX_SCREEN_W;
    if (sScreenH > MAX_SCREEN_H)
        sScreenH = MAX_SCREEN_H;
    sGameX = (sScreenW - gameW) / 2;
    PortSetMenuSize(sScreenW, sScreenH);  // menus are drawn at the screen's shape

    sStripY = gameH;
    sButtonR = (sScreenH - sStripY - 22) / 2;
    if (sButtonR > sScreenW * 16 / 100)
        sButtonR = sScreenW * 16 / 100;
    sButtonY = sStripY + 4 + (sScreenH - sStripY) / 2 - 4;
    sLeftX = 10 + sButtonR;
    sRightX = sScreenW - 10 - sButtonR;
    sPauseR = 11;
}

// Options -> SCALE changed: the same window, laid out for the new scale.
void PortTouchRelayout(void)
{
    Layout(sWindowW, sWindowH);
}

// A game (or bonus stage) is on: the cabinet is shown.
static int GameOn(void)
{
    return gMain.mainState == STATE_GAME_MAIN;
}

// The Mart or Evolution Select is open (the flipper buttons are arrows).
static int ShopOpen(void)
{
    return GameOn() && gMain.shopPanelActive;
}

// The Mart is open (the middle button says BACK).
static int MartOpen(void)
{
    return ShopOpen() && PortGameChoiceOpen();
}

// Menus where B goes back: they get a BACK button.
static int MenuBackShown(void)
{
    switch (gMain.mainState)
    {
    case STATE_OPTIONS:
    case STATE_POKEDEX:
    case STATE_EREADER:
    case STATE_SCORES_MAIN:
    case STATE_FIELD_SELECT:
    case STATE_BONUS_FIELD_SELECT:
        return TRUE;
    }
    return FALSE;
}

// The canvas last shown, and where it is on the screen: at the top in a
// game (in the middle without the on-screen controls); else centered (a
// menu canvas is the screen's size and fills it).
static int sCanvasW = PORT_CANVAS_WIDTH, sCanvasH = PORT_CANVAS_HEIGHT;

// The cabinet's round buttons are on.
static int HasButtons(void)
{
    return PortTouchControls() == PORT_TOUCH_BUTTONS || PortTouchControls() == PORT_TOUCH_PLAIN;
}

static int CanvasX(void)
{
    return (sScreenW - sCanvasW) / 2;
}

static int CanvasY(void)
{
    return GameOn() && HasButtons() ? 0 : (sScreenH - sCanvasH) / 2;
}

// ZONES: the pause button's middle, in the top right corner (in the black
// above the table where there's room).
static int CornerX(void)
{
    return sScreenW - MENU_BACK_X - sPauseR;
}

static int CornerY(void)
{
    int y = CanvasY() / 2;

    return y < MENU_BACK_Y + sPauseR ? y : MENU_BACK_Y + sPauseR;
}

static int Zones(void)
{
    return GameOn() && PortTouchControls() == PORT_TOUCH_ZONES;
}

// Where the game's canvas last shown is on the screen (for the LCD filter).
void PortTouchCanvasRect(int *x, int *y, int *w, int *h)
{
    *x = CanvasX();
    *y = CanvasY();
    *w = sCanvasW;
    *h = sCanvasH;
}

void PortTouchSetWindow(int windowW, int windowH)
{
    Layout(windowW, windowH);
}

#ifndef __ANDROID__
// JVPPBPORT_TOUCH=WxH: the window shape to lay out for on a PC.
void PortTouchTestWindow(int *w, int *h)
{
    const char *s = getenv("JVPPBPORT_TOUCH");

    *w = s ? atoi(s) : 0;
    *h = s && strchr(s, 'x') ? atoi(strchr(s, 'x') + 1) : 0;
    if (*w <= 0 || *h <= 0)
        *w = 1080, *h = 2340;
}
#endif

void PortTouchScreenSize(int *w, int *h)
{
    if (!sScreenW)
        Layout(0, 0);
    *w = sScreenW;
    *h = sScreenH;
}

// ---- Drawing ----

static uint32_t Mix(uint32_t a, uint32_t b, int t, int n)  // a..b, t of n
{
    int i;
    uint32_t out = 0xFF000000;

    if (t < 0)
        t = 0;
    if (t > n)
        t = n;
    for (i = 0; i < 24; i += 8)
    {
        int ca = (a >> i) & 0xFF, cb = (b >> i) & 0xFF;
        out |= (uint32_t)(ca + (cb - ca) * t / n) << i;
    }
    return out;
}

// Three-stop gradient: c0 at 0, c1 at mid (of n), c2 at n.
static uint32_t Gradient(uint32_t c0, uint32_t c1, uint32_t c2, int mid, int t, int n)
{
    return t < mid ? Mix(c0, c1, t, mid) : Mix(c1, c2, t - mid, n - mid);
}

static void Plot(int x, int y, uint32_t c)
{
    if (x >= 0 && x < sScreenW && y >= 0 && y < sScreenH)
        sScreen[y * sScreenW + x] = c;
}

static void Box(int x, int y, int w, int h, uint32_t c)
{
    int i, j;

    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            Plot(i, j, c);
}

static void ChromeBox(int x, int y, int w, int h, int vertical)
{
    int i, j;

    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
        {
            int t = vertical ? i : j, n = (vertical ? w : h) - 1;
            Plot(x + i, y + j, Gradient(0xFFF4F6F8, 0xFF9AA3AD, 0xFFE8EBEE, n / 2, t, n ? n : 1));
        }
}

static void Cabinet(void)
{
    // Ruby #E83A62 > #B0002E > #7D001F, Sapphire #3A8CF0 > #1458B8 > #0A3577 (cabinet colors).
    uint32_t c0 = sSapphire ? 0xFF3A8CF0 : 0xFFE83A62;
    uint32_t c1 = sSapphire ? 0xFF1458B8 : 0xFFB0002E;
    uint32_t c2 = sSapphire ? 0xFF0A3577 : 0xFF7D001F;
    int y, x;

    for (y = 0; y < sScreenH; y++)
    {
        uint32_t c = Gradient(c0, c1, c2, sScreenH * 6 / 10, y, sScreenH - 1);
        for (x = 0; x < sScreenW; x++)
            sScreen[y * sScreenW + x] = c;
    }
}

// A round button lit from the top left; pressed = sunk in and yellow.
static void RoundButton(int cx, int cy, int r, int pressed)
{
    int x, y, sink = pressed ? 3 : 0;
    uint32_t shadow = 0xFF28000A;

    // The hole's shadow under the button (smaller when pressed).
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r)
                Plot(cx + x, cy + y + 4, shadow);
    for (y = -r; y <= r; y++)
    {
        for (x = -r; x <= r; x++)
        {
            int d2 = x * x + y * y, hx = x + r / 4, hy = y + r * 36 / 100, h;
            uint32_t c;

            if (d2 > r * r)
                continue;
            if (d2 > (r - 3) * (r - 3))
                c = 0xFFD9DEE4;  // chrome rim
            else
            {
                h = (int)(SDL_sqrt((double)(hx * hx + hy * hy)) * 100 / (r * 2));  // 0 at the highlight
                c = pressed ? Gradient(0xFFFFFBE0, 0xFFFFE98A, 0xFFE0B43A, 50, h, 85)
                            : Gradient(0xFFFFFFFF, 0xFFE6EBF1, 0xFFA8B2BE, 45, h, 80);
            }
            Plot(cx + x, cy + y + sink, c);
        }
    }
}

// Distance from (px, py) to the segment (ax, ay)-(bx, by), and how far along it (0..1).
static double SegmentDistance(double px, double py, double ax, double ay, double bx, double by, double *along)
{
    double dx = bx - ax, dy = by - ay, t = ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy);

    if (t < 0)
        t = 0;
    if (t > 1)
        t = 1;
    *along = t;
    return SDL_sqrt((px - ax - t * dx) * (px - ax - t * dx) + (py - ay - t * dy) * (py - ay - t * dy));
}

// A pinball flipper, drawn from shapes like the game's: pale, outlined in
// blue, round at the pivot and narrow at the tip. dir 1 = pivot on the left.
static void Flipper(int cx, int cy, int r, int dir, int up)
{
    double len = r * 1.25, angle = (up ? -28.0 : 24.0) * 3.14159265 / 180.0;
    double ax = cx - dir * len / 2, ay = cy - (up ? -len * 0.12 : len * 0.12);
    double bx = ax + dir * len * SDL_cos(angle), by = ay + len * SDL_sin(angle);
    double r0 = r * 0.24, r1 = r * 0.11;
    int x, y;

    for (y = cy - r; y <= cy + r; y++)
    {
        for (x = cx - r; x <= cx + r; x++)
        {
            double t, d = SegmentDistance(x + 0.5, y + 0.5, ax, ay, bx, by, &t), w = r0 + (r1 - r0) * t;

            if (d > w)
                continue;
            if (d > w - 1.6)
                Plot(x, y, 0xFF2858A8);  // outline
            else
                Plot(x, y, y < ay + (by - ay) * t - w * 0.2 ? 0xFFF8FBFF : 0xFFC8DAF4);
        }
    }
    // The pivot.
    for (y = -3; y <= 3; y++)
        for (x = -3; x <= 3; x++)
            if (x * x + y * y <= 6)
                Plot((int)ax + x, (int)ay + y, x * x + y * y <= 2 ? 0xFF2858A8 : 0xFF8FB0E0);
}

static void PauseButton(int cx, int cy, int r, int pressed)
{
    int x, y;

    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
        {
            int d2 = x * x + y * y;
            if (d2 <= r * r)
                Plot(cx + x, cy + y, d2 > (r - 2) * (r - 2) ? 0xFFB8BEC6 : pressed ? 0xFF606060 : 0xFF301018);
        }
    Box(cx - 4, cy - 5, 3, 10, 0xFFFFFFFF);
    Box(cx + 2, cy - 5, 3, 10, 0xFFFFFFFF);
}

// A labelled button (BACK): dark, chrome-rimmed, rounded; pressed = lighter.
static void TextButton(int x, int y, int w, int h, const char *text, int pressed)
{
    int i, j, r = 5;

    for (j = 0; j < h; j++)
    {
        for (i = 0; i < w; i++)
        {
            int cx = i < r ? r - i : i >= w - r ? i - (w - r - 1) : 0;
            int cy = j < r ? r - j : j >= h - r ? j - (h - r - 1) : 0;
            int d2 = cx * cx + cy * cy;

            if (d2 > r * r)
                continue;
            if (d2 > (r - 2) * (r - 2) || i < 2 || j < 2 || i >= w - 2 || j >= h - 2)
                Plot(x + i, y + j, 0xFFB8BEC6);  // rim
            else
                Plot(x + i, y + j, pressed ? 0xFF606060 : 0xFF301018);
        }
    }
    PortDrawBoldText(x + (w - PortBoldTextWidth(text)) / 2, y + (h - 5) / 2, text, 0xFFFFFFFF, 0xFF000000, Plot);
}

// A bigger labelled button (the Mart's BACK): the text drawn twice as big.
static int sBigX, sBigY;
static void PlotBig(int x, int y, uint32_t c)
{
    int px = sBigX + x * 2, py = sBigY + y * 2;

    Box(px, py, 2, 2, c);
}

static void BigTextButton(int x, int y, int w, int h, const char *text, int pressed)
{
    TextButton(x, y, w, h, "", pressed);
    sBigX = x + (w - PortBoldTextWidth(text) * 2) / 2;
    sBigY = y + (h - 10) / 2;
    PortDrawBoldText(0, 0, text, 0xFFFFFFFF, 0xFF000000, PlotBig);
}

// An arrow on a round button (the Mart and Evolution Select): dir -1 left, 1 right.
static void ButtonArrow(int cx, int cy, int r, int dir)
{
    int x, y, half = r * 60 / 100;

    for (y = -half; y <= half; y++)
    {
        int len = (half - (y < 0 ? -y : y)) * 5 / 4;  // the triangle's width on this line
        for (x = 0; x <= len; x++)
            Plot(cx + dir * (x - half * 5 / 8), cy + y, x == len || y == -half || y == half ? 0xFF1A3A78 : 0xFF2858A8);
    }
}

// The same pointing up (dir -1) or down.
static void ButtonArrowV(int cx, int cy, int r, int dir)
{
    int x, y, half = r * 60 / 100;

    for (x = -half; x <= half; x++)
    {
        int len = (half - (x < 0 ? -x : x)) * 5 / 4;
        for (y = 0; y <= len; y++)
            Plot(cx + x, cy + dir * (y - half * 5 / 8), y == len || x == -half || x == half ? 0xFF1A3A78 : 0xFF2858A8);
    }
}

static int ZoneHeld(int zone)
{
    int i;

    for (i = 0; i < sFingerCount; i++)
        if (sFingers[i].zone == zone)
            return TRUE;
    return FALSE;
}

// ---- The shops' big buttons, on the panel (GBA screen places) ----
#define SHOP_R 17            // the round arrow buttons
#define SHOP_LEFT_X 18       // beside the picture (over the panel's own arrows)
#define SHOP_RIGHT_X 126
#define SHOP_MID_X 74        // over and under the picture (Evolution Select)
#define SHOP_MID_Y 104
#define SHOP_UP_Y 64
#define SHOP_DOWN_Y 144
#define SHOP_BACK_Y 152      // under the panel
#define SHOP_BACK_W 96
#define SHOP_BACK_H 32

static void ShopOverlay(void)
{
    int arrows = PortShopArrows(), x, y;

    if (!arrows)
        return;
    PortScreenToCanvas(0, 0, &x, &y);
    x += CanvasX();
    y += CanvasY();
    if (arrows & PORT_SHOP_SIDE)
    {
        int left = ZoneHeld(ZONE_LEFT), right = ZoneHeld(ZONE_RIGHT);

        RoundButton(x + SHOP_LEFT_X, y + SHOP_MID_Y, SHOP_R, left);
        ButtonArrow(x + SHOP_LEFT_X, y + SHOP_MID_Y + (left ? 3 : 0), SHOP_R, -1);
        RoundButton(x + SHOP_RIGHT_X, y + SHOP_MID_Y, SHOP_R, right);
        ButtonArrow(x + SHOP_RIGHT_X, y + SHOP_MID_Y + (right ? 3 : 0), SHOP_R, 1);
    }
    if (arrows & PORT_SHOP_UP)
    {
        RoundButton(x + SHOP_MID_X, y + SHOP_UP_Y, SHOP_R - 3, ZoneHeld(ZONE_UP));
        ButtonArrowV(x + SHOP_MID_X, y + SHOP_UP_Y + (ZoneHeld(ZONE_UP) ? 3 : 0), SHOP_R - 3, -1);
    }
    if (arrows & PORT_SHOP_DOWN)
    {
        RoundButton(x + SHOP_MID_X, y + SHOP_DOWN_Y, SHOP_R - 3, ZoneHeld(ZONE_DOWN));
        ButtonArrowV(x + SHOP_MID_X, y + SHOP_DOWN_Y + (ZoneHeld(ZONE_DOWN) ? 3 : 0), SHOP_R - 3, 1);
    }
    if (arrows & PORT_SHOP_BACK)
        BigTextButton(x + 120 - SHOP_BACK_W / 2, y + SHOP_BACK_Y - SHOP_BACK_H / 2, SHOP_BACK_W, SHOP_BACK_H, "BACK", ZoneHeld(ZONE_PAUSE));
}

// The button of the shop's big buttons at this screen place, or ZONE_NONE.
static int ShopZoneAt(float fx, float fy)
{
    int arrows = PortShopArrows(), x, y, sx, sy;

    if (!arrows)
        return ZONE_NONE;
    PortScreenToCanvas(0, 0, &x, &y);
    sx = (int)fx - CanvasX() - x;
    sy = (int)fy - CanvasY() - y;
    if ((arrows & PORT_SHOP_BACK) && sx >= 120 - SHOP_BACK_W / 2 - 6 && sx < 120 + SHOP_BACK_W / 2 + 6
        && sy >= SHOP_BACK_Y - SHOP_BACK_H / 2 - 6 && sy < SHOP_BACK_Y + SHOP_BACK_H / 2 + 6)
        return ZONE_PAUSE;
    if ((arrows & PORT_SHOP_SIDE) && sy >= SHOP_MID_Y - 34 && sy < SHOP_MID_Y + 30)
    {
        if (sx < 37)
            return ZONE_LEFT;
        if (sx >= 112 && sx < 143)
            return ZONE_RIGHT;
    }
    if (sx >= 37 && sx < 112)
    {
        if ((arrows & PORT_SHOP_UP) && sy >= SHOP_UP_Y - 20 && sy < 80)
            return ZONE_UP;
        if ((arrows & PORT_SHOP_DOWN) && sy >= 126 && sy < SHOP_DOWN_Y + 20)
            return ZONE_DOWN;
    }
    return ZONE_NONE;
}

// Field Select's Ball Speed arrows: the middle of the left one (dir -1) or the
// right one, on the screen. FALSE if they aren't shown.
static int SpeedArrow(int dir, int *cx, int *cy)
{
    int x, y, w, h;

    if (!PortFieldSpeedRect(&x, &y, &w, &h))
        return FALSE;
    *cx = CanvasX() + (dir < 0 ? x - SPEED_GAP - SPEED_R : x + w + SPEED_GAP + SPEED_R);
    *cy = CanvasY() + y + h / 2;
    return TRUE;
}

// The whole screen: the game's canvas (w x h) inside the cabinet, moved by
// dx, dy (tilt, see PortTableWobble). Returns the finished screen; its size
// is PortTouchScreenSize's.
const uint32_t *PortTouchCompose(const uint32_t *canvas, int w, int h, int dx, int dy)
{
    int y, field;

    if (!sScreenW)
        Layout(0, 0);
    if (gMain.selectedField == FIELD_RUBY || gMain.selectedField == FIELD_SAPPHIRE)
        sSapphire = gMain.selectedField == FIELD_SAPPHIRE;
    else if (PortLastTable(&field))
        sSapphire = field == FIELD_SAPPHIRE;
    sCanvasW = w;
    sCanvasH = h;
    if (!GameOn())
    {
        if (w == sScreenW && h == sScreenH && !MenuBackShown())
            return canvas;
        memset(sScreen, 0, sScreenW * sScreenH * 4);
        for (y = 0; y < h && CanvasY() + y < sScreenH; y++)
            if (CanvasX() >= 0 && CanvasX() + w <= sScreenW)
                memcpy(&sScreen[(CanvasY() + y) * sScreenW + CanvasX()], &canvas[y * w], w * 4);
        if (MenuBackShown())
            TextButton(MENU_BACK_X, MENU_BACK_Y, MENU_BACK_W, MENU_BACK_H, "< BACK", ZoneHeld(ZONE_PAUSE));
        {
            int dir, cx, cy, zone;

            for (dir = -1; dir <= 1; dir += 2)
            {
                if (!SpeedArrow(dir, &cx, &cy))
                    break;
                zone = dir < 0 ? ZONE_LEFT : ZONE_RIGHT;
                RoundButton(cx, cy, SPEED_R, ZoneHeld(zone));
                ButtonArrow(cx, cy + (ZoneHeld(zone) ? 3 : 0), SPEED_R, dir);
            }
        }
        return sScreen;
    }
    if (Zones())
        memset(sScreen, 0, sScreenW * sScreenH * 4);
    else
        Cabinet();
    for (y = 0; y < h; y++)
    {
        int x0 = sGameX + dx, y0 = CanvasY() + y + dy, from = 0, n = w;

        if (y0 < 0 || y0 >= sScreenH)
            continue;
        if (x0 < 0)
            from = -x0, n += x0, x0 = 0;
        if (x0 + n > sScreenW)
            n = sScreenW - x0;
        if (n > 0)
            memcpy(&sScreen[y0 * sScreenW + x0], &canvas[y * w + from], n * 4);
    }
    if (Zones())
    {
        if (!MartOpen())
            PauseButton(CornerX(), CornerY(), sPauseR, ZoneHeld(ZONE_PAUSE));
        ShopOverlay();
        return sScreen;
    }
    if (sGameX >= 3)
    {
        ChromeBox(sGameX - 3, CanvasY(), 3, h, 1);
        ChromeBox(sGameX + w, CanvasY(), 3, h, 1);
    }
    ChromeBox(0, CanvasY() + h, sScreenW, 3, 0);
    if (!PortTouchControls())
    {
        if (CanvasY() >= 3)
            ChromeBox(0, CanvasY() - 3, sScreenW, 3, 0);
        ShopOverlay();
        return sScreen;
    }

    // (In the Mart and Evolution Select they look the same but press
    // Left / Right; the panel has its own big arrows.)
    {
        RoundButton(sLeftX, sButtonY, sButtonR, ZoneHeld(ZONE_LEFT));
        RoundButton(sRightX, sButtonY, sButtonR, ZoneHeld(ZONE_RIGHT));
        if (PortTouchControls() == PORT_TOUCH_BUTTONS)
        {
            Flipper(sLeftX, sButtonY + (ZoneHeld(ZONE_LEFT) ? 3 : 0), sButtonR, 1, ZoneHeld(ZONE_LEFT));
            Flipper(sRightX, sButtonY + (ZoneHeld(ZONE_RIGHT) ? 3 : 0), sButtonR, -1, ZoneHeld(ZONE_RIGHT));
        }
    }
    if (!MartOpen())
        PauseButton(sScreenW / 2, sButtonY, sPauseR, ZoneHeld(ZONE_PAUSE));
    ShopOverlay();
    return sScreen;
}

// ---- Fingers ----

static int Playing(void)
{
    return PortPlaying();
}

// A finger on Spoink, the launcher (the same spot on both main tables;
// generous, it's a small sprite).
static int SpoinkAt(float x, float y)
{
    int tx = (int)x - sGameX, ty = (int)y - CanvasY();

    return gMain.selectedField < MAIN_FIELD_COUNT && tx >= 218 && tx < PORT_CANVAS_WIDTH && ty >= 356 && ty < 444;
}

static int ZoneAt(float x, float y)
{
    int dx = (int)x - sScreenW / 2, dy = (int)y - sButtonY, r = sPauseR + 8, zone;

    if (!GameOn())
    {
        if (MenuBackShown() && x >= MENU_BACK_X - 8 && x < MENU_BACK_X + MENU_BACK_W + 8
            && y >= MENU_BACK_Y - 8 && y < MENU_BACK_Y + MENU_BACK_H + 8)
            return ZONE_PAUSE;  // Esc: back
        for (zone = -1; zone <= 1; zone += 2)
        {
            int cx, cy;

            if (SpeedArrow(zone, &cx, &cy) && (int)x >= cx - SPEED_R - 6 && (int)x <= cx + SPEED_R + 6
                && (int)y >= cy - SPEED_R - 8 && (int)y <= cy + SPEED_R + 8)
                return zone < 0 ? ZONE_LEFT : ZONE_RIGHT;
        }
        return ZONE_MOUSE;
    }
    if ((zone = ShopZoneAt(x, y)) != ZONE_NONE)
        return zone;
    if (!PortTouchControls())
        return Playing() ? ZONE_SWIPE : ZONE_MOUSE;  // swipes don't tilt then
    if (Zones())
    {
        int cx = (int)x - CornerX(), cy = (int)y - CornerY();

        if (!MartOpen() && cx * cx + cy * cy <= r * r)
            return ZONE_PAUSE;
        if (Playing() && !PortGameChoiceOpen())
            return x < sScreenW / 2 ? ZONE_LEFT : ZONE_RIGHT;
        return Playing() ? ZONE_SWIPE : ZONE_MOUSE;
    }
    if (y >= sStripY)
    {
        if (MartOpen())
            return dx < 0 ? ZONE_LEFT : ZONE_RIGHT;  // its BACK is under the panel
        if (dx * dx + dy * dy <= r * r)
            return ZONE_PAUSE;
        if (dx > -r && dx < r)
            return ZONE_NONE;  // around the pause button
        return dx < 0 ? ZONE_LEFT : ZONE_RIGHT;
    }
    if (Playing() && SpoinkAt(x, y))
        return ZONE_LAUNCH;
    return Playing() ? ZONE_SWIPE : ZONE_MOUSE;
}

// A finger that may tilt moved: past SWIPE_MIN it tilts once that way.
static void TiltSwipe(int i, float x, float y)
{
    float dx = x - sFingers[i].x0, dy = y - sFingers[i].y0;

    if (sFingers[i].used || dx * dx + dy * dy < SWIPE_MIN * SWIPE_MIN)
        return;
    sFingers[i].used = TRUE;  // not a tap
    if (!PortTouchControls())
        return;
    if (dx * dx > dy * dy)
        sTiltAction = dx < 0 ? PINBALL_INPUT_TILT_LEFT : PINBALL_INPUT_TILT_RIGHT;
    else
        sTiltAction = dy < 0 ? PINBALL_INPUT_TILT_UP : -1;  // down: nothing
    sTiltFrames = TILT_FRAMES;
    if (getenv("JVPPBPORT_MOUSE_TRACE"))
        fprintf(stderr, "touch: tilt action %d on frame %u\n", sTiltAction, PortCurrentFrame());
}

static int FindFinger(SDL_FingerID id)
{
    int i;

    for (i = 0; i < sFingerCount; i++)
        if (sFingers[i].id == id)
            return i;
    return -1;
}

// The mouse event for a finger that works like the mouse, in the game
// canvas's pixels. Returns 1 if *event became one.
static int AsMouse(SDL_Event *event, int type, float x, float y)
{
    int mx = (int)x - CanvasX(), my = (int)y - CanvasY();
    SDL_Event out;

    memset(&out, 0, sizeof(out));
    out.type = type;
    if (type == SDL_MOUSEMOTION)
    {
        out.motion.which = SDL_TOUCH_MOUSEID;
        out.motion.x = mx;
        out.motion.y = my;
        out.motion.state = SDL_BUTTON_LMASK;
    }
    else
    {
        out.button.which = SDL_TOUCH_MOUSEID;
        out.button.button = SDL_BUTTON_LEFT;
        out.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
        out.button.clicks = 1;
        out.button.x = mx;
        out.button.y = my;
    }
    *event = out;
    return 1;
}

// A click where the finger was lifted: the button goes down now (handed
// to input.c here) and up in *event.
static int ClickAt(SDL_Event *event, float x, float y)
{
    SDL_Event down;

    AsMouse(&down, SDL_MOUSEBUTTONDOWN, x, y);
    PortInputEvent(&down);
    return AsMouse(event, SDL_MOUSEBUTTONUP, x, y);
}

// A menu finger moved: past SWIPE_MIN it's a swipe along the way it went,
// pressing the D-pad that way (as wheel notches, + = up / right); each
// SWIPE_STEP further presses again.
static void MenuSwipe(int i, float x, float y)
{
    float dx = x - sFingers[i].x0, dy = y - sFingers[i].y0, d;
    int steps;
    SDL_Event wheel;

    if (!sFingers[i].used)
    {
        if (dx * dx + dy * dy < SWIPE_MIN * SWIPE_MIN)
            return;
        sFingers[i].used = TRUE;
        sFingers[i].axis = dx * dx < dy * dy;
        sFingers[i].lastY = y;
    }
    // Options, if it's taller than the screen: up/down drags it.
    if (sFingers[i].axis && gMain.mainState == STATE_OPTIONS && !PortConfirmOpen()
        && PortOptionsScrollBy((int)(y - sFingers[i].lastY)))
    {
        sFingers[i].lastY += (int)(y - sFingers[i].lastY);
        return;
    }
    d = sFingers[i].axis ? -dy : dx;
    steps = d >= SWIPE_MIN ? 1 + (int)((d - SWIPE_MIN) / SWIPE_STEP)
          : d <= -SWIPE_MIN ? -1 - (int)((-d - SWIPE_MIN) / SWIPE_STEP) : 0;
    if (steps == sFingers[i].steps)
        return;
    memset(&wheel, 0, sizeof(wheel));
    wheel.type = SDL_MOUSEWHEEL;
    wheel.wheel.which = SDL_TOUCH_MOUSEID;
    if (sFingers[i].axis)
        wheel.wheel.y = steps - sFingers[i].steps;
    else
        wheel.wheel.x = steps - sFingers[i].steps;
    sFingers[i].steps = steps;
    PortInputEvent(&wheel);
    if (getenv("JVPPBPORT_MOUSE_TRACE"))
        fprintf(stderr, "touch: swipe %s %d on frame %u\n", sFingers[i].axis ? "up/down" : "across", wheel.wheel.x + wheel.wheel.y, PortCurrentFrame());
}

// The Mart's / Evolution Select's arrow buttons: Left or Right once (as a
// sideways wheel notch, which mouse.c turns into the D-pad).
static void ShopArrow(int dir)
{
    SDL_Event wheel;

    memset(&wheel, 0, sizeof(wheel));
    wheel.type = SDL_MOUSEWHEEL;
    wheel.wheel.which = SDL_TOUCH_MOUSEID;
    wheel.wheel.x = dir;
    PortInputEvent(&wheel);
}

// Evolution Select's arrows over and under the picture: Up or Down once.
static void ShopArrowUpDown(int dir)
{
    SDL_Event wheel;

    memset(&wheel, 0, sizeof(wheel));
    wheel.type = SDL_MOUSEWHEEL;
    wheel.wheel.which = SDL_TOUCH_MOUSEID;
    wheel.wheel.y = dir;
    PortInputEvent(&wheel);
}

// Touch handling for an event from SDL (PortPollEvent). Fingers on the
// cabinet and swipes are used up here (returns 0); fingers that work like
// the mouse come back as mouse events (returns 1, *event changed); a real
// mouse's events are moved into the game canvas's pixels.
int PortTouchEvent(SDL_Event *event)
{
    float x, y;
    int i;

    if (!PortTouchEnabled())
        return 1;
    switch (event->type)
    {
    case SDL_FINGERDOWN:
        PortWindowToPicture(event->tfinger.x, event->tfinger.y, &x, &y);
        if (sFingerCount == MAX_FINGERS || FindFinger(event->tfinger.fingerId) >= 0)
            return 0;
        i = sFingerCount++;
        sFingers[i].id = event->tfinger.fingerId;
        sFingers[i].zone = ZoneAt(x, y);
        sFingers[i].x0 = x;
        sFingers[i].y0 = y;
        sFingers[i].used = FALSE;
        sFingers[i].steps = 0;
        sFingers[i].held = FALSE;
        // Only one finger works the menus. It points there now; the click
        // comes when it's lifted (if it didn't swipe). On the Pokédex list
        // and its scroll bar it presses at once and drags, as the mouse does.
        if (sFingers[i].zone == ZONE_MOUSE && !ZoneHeld(ZONE_MENU))
        {
            sFingers[i].zone = ZONE_MENU;
            if (PortMouseDragSpot((int)x - CanvasX(), (int)y - CanvasY()))
            {
                SDL_Event point;

                sFingers[i].held = TRUE;
                AsMouse(&point, SDL_MOUSEMOTION, x, y);
                PortInputEvent(&point);
                return AsMouse(event, SDL_MOUSEBUTTONDOWN, x, y);
            }
            return AsMouse(event, SDL_MOUSEMOTION, x, y);
        }
        if (sFingers[i].zone == ZONE_PAUSE)
            sPausePressed = TRUE;
        if ((ShopOpen() || !GameOn()) && (sFingers[i].zone == ZONE_LEFT || sFingers[i].zone == ZONE_RIGHT))
            ShopArrow(sFingers[i].zone == ZONE_LEFT ? -1 : 1);
        if (sFingers[i].zone == ZONE_UP || sFingers[i].zone == ZONE_DOWN)
            ShopArrowUpDown(sFingers[i].zone == ZONE_UP ? 1 : -1);
        return 0;
    case SDL_FINGERMOTION:
        PortWindowToPicture(event->tfinger.x, event->tfinger.y, &x, &y);
        i = FindFinger(event->tfinger.fingerId);
        if (i < 0)
            return 0;
        if (sFingers[i].held)
            return AsMouse(event, SDL_MOUSEMOTION, x, y);
        if (sFingers[i].zone == ZONE_MENU)
        {
            MenuSwipe(i, x, y);
            return sFingers[i].used ? 0 : AsMouse(event, SDL_MOUSEMOTION, x, y);
        }
        // ZONES: a flipper finger swipes to tilt too.
        if (sFingers[i].zone == ZONE_SWIPE
            || (Zones() && (sFingers[i].zone == ZONE_LEFT || sFingers[i].zone == ZONE_RIGHT)))
            TiltSwipe(i, x, y);
        return 0;
    case SDL_FINGERUP:
        PortWindowToPicture(event->tfinger.x, event->tfinger.y, &x, &y);
        i = FindFinger(event->tfinger.fingerId);
        if (i < 0)
            return 0;
        {
            int zone = sFingers[i].zone, swiped = sFingers[i].used, held = sFingers[i].held;

            sFingers[i] = sFingers[--sFingerCount];
            if (held)
                return AsMouse(event, SDL_MOUSEBUTTONUP, x, y);
            // A tap: in a menu, or on the table while playing (A for the
            // game's prompts; mouse.c presses A for a click there).
            if ((zone == ZONE_MENU || zone == ZONE_SWIPE) && !swiped)
                return ClickAt(event, x, y);
        }
        return 0;
    case SDL_MOUSEMOTION:
        if (event->motion.which == SDL_TOUCH_MOUSEID)
            return 0;  // SDL's own copy of a finger
        event->motion.x -= CanvasX();
        event->motion.y -= CanvasY();
        return 1;
    case SDL_MOUSEWHEEL:
        return event->wheel.which != SDL_TOUCH_MOUSEID;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        if (event->button.which == SDL_TOUCH_MOUSEID)
            return 0;
        event->button.x -= CanvasX();
        event->button.y -= CanvasY();
        return 1;
    }
    return 1;
}

// Once per frame (input.c): counts down a tilt; returns 1 if the pause
// button was pressed since the last frame.
// JVPPBPORT_FINGER (see the top): the script's steps for this frame.
static void RunFingerScript(void)
{
    static const char *script;
    static int read;
    const char *p;

    if (!read++)
        script = getenv("JVPPBPORT_FINGER");
    for (p = script; p && *p; p = strchr(p, ',') ? strchr(p, ',') + 1 : NULL)
    {
        char kind[8] = "";
        unsigned frame;
        float x, y;
        SDL_Event event;

        if (sscanf(p, "%u:%7[a-z]:%f:%f", &frame, kind, &x, &y) != 4 || frame != PortCurrentFrame())
            continue;
        memset(&event, 0, sizeof(event));
        event.type = strcmp(kind, "down") == 0 ? SDL_FINGERDOWN : strcmp(kind, "up") == 0 ? SDL_FINGERUP : SDL_FINGERMOTION;
        event.tfinger.fingerId = 1;
        event.tfinger.x = x / sScreenW;
        event.tfinger.y = y / sScreenH;
        if (PortTouchEvent(&event))
            PortInputEvent(&event);
    }
}

int PortTouchFrame(void)
{
    int pause;

    RunFingerScript();
    pause = sPausePressed;

    if (sTiltFrames && --sTiltFrames == 0)
        sTiltAction = -1;
    sPausePressed = FALSE;
    return pause;
}

// Lets go of every finger (the app went to the background: their lifts
// may never come).
void PortTouchReset(void)
{
    int i;

    for (i = 0; i < sFingerCount; i++)
    {
        if (sFingers[i].held)
        {
            SDL_Event up;

            AsMouse(&up, SDL_MOUSEBUTTONUP, sFingers[i].x0, sFingers[i].y0);
            PortInputEvent(&up);
        }
    }
    sFingerCount = 0;
    sTiltAction = -1;
    sTiltFrames = 0;
    sPausePressed = FALSE;
}

int PortTouchActionHeld(int action)
{
    if (!PortTouchEnabled())
        return FALSE;
    if (action == PINBALL_INPUT_LEFT_FLIPPER)
        return ZoneHeld(ZONE_LEFT);
    if (action == PINBALL_INPUT_RIGHT_FLIPPER)
        return ZoneHeld(ZONE_RIGHT);
    if (action == PORT_PINBALL_LAUNCH)
        return ZoneHeld(ZONE_RIGHT) || ZoneHeld(ZONE_LAUNCH);
    return action == sTiltAction;
}
