// JVPPBPort: the two windows of Options' SCREEN box. Its rows only show the
// value being chosen (screenmenu.c); nothing changes until CONFIRM CHANGES.
//   KEEP CHANGES?  After a change to the picture (FULLSCREEN, TATE MODE, FILTER,
//                  SCALE): it shows at once, with KEEP, UNDO and a countdown,
//                  and without a KEEP it goes back by itself, so a setting that
//                  makes the menu hard to see or touch can't trap anyone.
//   SAVE CHANGES?  On leaving Options with choices not confirmed: SAVE (then
//                  KEEP CHANGES? if the picture changes), DISCARD; BACK stays.
//
// Nothing is written to the settings file until KEEP (PortSettingsHold), so
// a crash or a closed app during the countdown leaves the old settings.
//
// The windows are built from the Options screen's own lines (the orange frame,
// the yellow-topped white box), as the port's boxes are (ppu.c), and scaled up
// by a whole number from the canvas, so they stay big at every scale.
// Keys and pads: Left/Right pick a button, OK presses it, BACK is UNDO or
// stays (src/options.c); the mouse and fingers use them as menu items (mouse.c).
#include "jvppbport_layout.h"
#include <stdio.h>
#include <string.h>
#include "global.h"
#include "m4a.h"
#include "main.h"
#include "constants/bg_music.h"
#include "jvppbport.h"

#define SECONDS 10
#define FRAMES_PER_SECOND 60
#define INPUT_LOCK 24  // frames OK is ignored at first, against double presses

enum { CLOSED, ASK_KEEP, ASK_SAVE };

static int sMode, sFramesLeft, sSelected, sLock, sLeaveOnKeep, sLeave;
static int sFullscreen, sTate, sFilter, sScale, sBonus;

int PortConfirmOpen(void)
{
    return sMode != CLOSED;
}

int PortConfirmSelected(void)
{
    return sSelected;
}

void PortConfirmSelect(int choice)
{
    sSelected = choice;
}

// Options should be left now (asked once).
int PortConfirmTakeLeave(void)
{
    int leave = sLeave;

    sLeave = FALSE;
    return leave;
}

// KEEP CHANGES?: call before changing the settings. leaveOnKeep: KEEP also
// leaves Options (the changes came from SAVE CHANGES?).
void PortConfirmBegin(int leaveOnKeep)
{
    sFullscreen = PortFullscreen();
    sTate = PortTate();
    sFilter = PortLcdFilter();
    sScale = PortScaleMode();
    sBonus = PortBonusFull();
    PortWindowRemember();
    PortSettingsHold(TRUE);
    sMode = ASK_KEEP;
    sLeaveOnKeep = leaveOnKeep;
    sFramesLeft = SECONDS * FRAMES_PER_SECOND;
    sSelected = PORT_CONFIRM_KEEP;
    sLock = INPUT_LOCK;
}

// SAVE CHANGES?, on leaving Options.
void PortConfirmAskSave(void)
{
    sMode = ASK_SAVE;
    sSelected = PORT_CONFIRM_KEEP;
    sLock = INPUT_LOCK;
}

static void FinishKeep(int keep)
{
    sMode = CLOSED;
    if (!keep)
    {
        PortSetFullscreen(sFullscreen);
        PortSetTate(sTate);
        PortSetLcdFilter(sFilter);
        PortSetScaleMode(sScale);
        if (sBonus != PortBonusFull())
            PortSetBonusFull(sBonus);
        PortWindowRestore();
    }
    PortSettingsHold(FALSE);
    PortSettingsSave();
    if (keep && sLeaveOnKeep)
        sLeave = TRUE;
}

// BACK: KEEP CHANGES? undoes; SAVE CHANGES? closes and stays in Options.
void PortConfirmBack(void)
{
    if (sMode == ASK_KEEP)
        FinishKeep(FALSE);
    else
        sMode = CLOSED;
}

// The program is closing: the old settings stay.
void PortConfirmAbort(void)
{
    if (sMode == ASK_KEEP)
        FinishKeep(FALSE);
    sMode = CLOSED;
}

// OK on the chosen button. Returns FALSE while OK is still locked.
int PortConfirmPress(void)
{
    if (sLock > 0)
        return FALSE;
    if (sMode == ASK_KEEP)
        FinishKeep(sSelected == PORT_CONFIRM_KEEP);
    else if (sSelected == PORT_CONFIRM_KEEP)
    {
        sMode = CLOSED;
        if (!PortOptionsApply(TRUE))
            sLeave = TRUE;  // nothing to check: done
    }
    else
    {
        sMode = CLOSED;
        PortOptionsDiscard();
        sLeave = TRUE;
    }
    return TRUE;
}

// Once per frame: the countdown (and the old settings if Options was left
// some other way, such as the game's soft reset).
void PortConfirmFrame(void)
{
    if (gMain.mainState != STATE_OPTIONS)
    {
        PortOptionsDiscard();
        if (sMode != CLOSED)
            PortConfirmAbort();
        return;
    }
    if (sMode == CLOSED)
        return;
    if (sLock > 0)
        sLock--;
    if (sMode == ASK_KEEP && --sFramesLeft <= 0)
    {
        FinishKeep(FALSE);
        m4aSongNumStart(SE_MENU_CANCEL);
    }
}

// ---- The window: a small version of Options' boxes, in window units ----

#define WIN_W 120
#define WIN_H 72
#define FRAME 4         // orange around the box
#define BOX_TOP 12      // the box's yellow top with its name tab
#define BOX_BOTTOM 4    // its rounded bottom
#define EDGE 4
#define NAME_X 17       // the name in the tab (as the boxes')
#define TEXT_Y 24       // the line of text
#define BUTTON_Y 36
#define BUTTON_W 44
#define BUTTON_H 22
#define BUTTON_GAP 8

static int Unit(void)  // canvas pixels per window unit
{
    int a = PortCanvasWidth() / 128, b = PortCanvasHeight() / 224, u = a < b ? a : b;

    return u < 1 ? 1 : u;
}

static void WindowOrigin(int *x, int *y)
{
    *x = (PortCanvasWidth() - WIN_W * Unit()) / 2;
    *y = (PortCanvasHeight() - WIN_H * Unit()) / 2;
}

// Button `which`, in canvas pixels (first on the left).
void PortConfirmButton(int which, int *x, int *y, int *w, int *h)
{
    int u = Unit(), wx, wy;

    WindowOrigin(&wx, &wy);
    *w = BUTTON_W * u;
    *h = BUTTON_H * u;
    *x = wx + ((WIN_W - 2 * BUTTON_W - BUTTON_GAP) / 2 + which * (BUTTON_W + BUTTON_GAP)) * u;
    *y = wy + BUTTON_Y * u;
}

static uint32_t *sCanvas;
static int sPlotX, sPlotY, sPlotU;

static void Fill(int x, int y, int w, int h, uint32_t c)
{
    int i, j, cw = PortCanvasWidth(), ch = PortCanvasHeight();

    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            if (i >= 0 && i < cw && j >= 0 && j < ch)
                sCanvas[j * cw + i] = c;
}

static void TextPlot(int x, int y, uint32_t c)
{
    Fill(sPlotX + x * sPlotU, sPlotY + y * sPlotU, sPlotU, sPlotU, c);
}

static void Text(int x, int y, const char *s)
{
    int u = Unit(), wx, wy;

    WindowOrigin(&wx, &wy);
    sPlotU = u;
    sPlotX = wx + x * u;
    sPlotY = wy + y * u;
    PortDrawBoldText(0, 0, s, 0xFFFFFFFF, 0xFF000000, TextPlot);
}

// The Options screen line and column for window unit (x, y), the way ppu.c
// builds its boxes. The tab is cut to fit the name; the middle of the source
// lines (all alike) is left out.
static uint32_t WindowPixel(int x, int y, int nameEnd)
{
    // (EDGE: the screen's own outer columns, grey and black, are left out.)
    int sx = x < WIN_W / 2 ? x + EDGE : x + PORT_SCREEN_WIDTH - WIN_W - EDGE, sy, k = y - FRAME, boxH = WIN_H - 2 * FRAME;

    if (y < FRAME || y >= WIN_H - FRAME)
        sy = 158 + (y & 1);  // the frame
    else if (k < BOX_TOP)
    {
        sy = 64 + k;
        if (x >= NAME_X - 1 - EDGE && x < nameEnd)  // (over the game's own box name)
            sx = 15;
        else if (x >= nameEnd && x < nameEnd + 14)
            sx = 69 + x - nameEnd;  // the tab's slanted edge
        else if (x >= nameEnd + 14 && x < WIN_W - 24)
            sx = 120;
    }
    else if (k < boxH - BOX_BOTTOM)
        sy = 76 + (y & 1);
    else
        sy = 148 + k - (boxH - BOX_BOTTOM);
    return PortOptionsLinePixel(sx, sy);
}

void PortConfirmDraw(uint32_t *canvas)
{
    const char *title, *names[2];
    char text[32];
    int i, n, x, y, w, h, u = Unit(), wx, wy, nameEnd;
    uint32_t yellow, orange, white;

    if (sMode == CLOSED)
        return;
    sCanvas = canvas;
    // The screen behind, dimmed, so the orange window stands out on the orange menu.
    n = PortCanvasWidth() * PortCanvasHeight();
    for (i = 0; i < n; i++)
        canvas[i] = 0xFF000000 | ((canvas[i] >> 1) & 0x7F7F7F);
    if (sMode == ASK_KEEP)
    {
        title = "KEEP CHANGES?";
        names[0] = "KEEP", names[1] = "UNDO";
        snprintf(text, sizeof(text), "UNDOING IN %d", (sFramesLeft + FRAMES_PER_SECOND - 1) / FRAMES_PER_SECOND);
    }
    else
    {
        title = "SAVE CHANGES?";
        names[0] = "SAVE", names[1] = "DISCARD";
        snprintf(text, sizeof(text), "SCREEN SETTINGS CHANGED");
    }
    nameEnd = NAME_X + PortBoldTextWidth(title) + 1;
    WindowOrigin(&wx, &wy);
    for (y = 0; y < WIN_H; y++)
        for (x = 0; x < WIN_W; x++)
            Fill(wx + x * u, wy + y * u, u, u, WindowPixel(x, y, nameEnd));
    Text(NAME_X, FRAME + 4, title);
    Text((WIN_W - PortBoldTextWidth(text)) / 2, TEXT_Y, text);
    // The game's own colors: the tab's yellow, the frame's orange, the box's white.
    yellow = PortOptionsLinePixel(15, 72);
    orange = PortOptionsLinePixel(120, 158);
    white = PortOptionsLinePixel(120, 76);
    for (i = 0; i < 2; i++)
    {
        int textX;

        PortConfirmButton(i, &x, &y, &w, &h);
        Fill(x, y, w, h, orange);
        Fill(x + 2 * u, y + 2 * u, w - 4 * u, h - 4 * u, i == sSelected ? yellow : white);
        textX = (x - wx) / u + (BUTTON_W - PortBoldTextWidth(names[i])) / 2;
        Text(textX, (y - wy) / u + (BUTTON_H - 5) / 2, names[i]);
    }
}
