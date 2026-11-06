// JVPPBPort: the boxes the port puts on the Options screen in place of the
// GBA's BUTTON CONFIG box and under it (drawn by ppu.c, worked by
// src/options.c and mouse.c):
//   PINBALL KEYS (in place of BUTTON CONFIG's TYPE A-D and EDIT: one set
//   of keys, each action free to change)
//     TOUCH   < BUTTONS / ZONES / OFF >   touch screens only: the on-screen
//                            flipper and pause buttons, or the screen's
//                            halves as flippers, or neither (touch.c)
//     LEFT FLIPPER ... TILT UP   the keys of each pinball action (input.c); A sets them
//     DEFAULTS    A puts the default keys back
//   SCREEN
//     FULLSCREEN < OFF / ON >          borderless fullscreen, as Alt+Enter or
//                                        F11 (platform.c; not on touch screens)
//     FILTER  < OFF / LCD / GBA >        the LCD filter (lcd.c)
//     SCALE   < FILL / SNAP FILL / 1X / 2X ... >  the window's scale (platform.c; not
//                                        on touch screens, always FILL there)
//     (a note under SCALE: filters look best at whole-number scales)
//   MENU KEYS
//     UP ... R    the keys of each menu action (input.c); A sets them
//     SAVE AND QUIT  (PC) saves a game in progress and closes the program
//     DEFAULTS    A puts the default keys back
//   E-READER
//     UNLOCK / LOCK E-READER EXCLUSIVES   A marks Chikorita, Cyndaquil,
//                 Totodile and Aerodactyl "shared" in the Pokédex, as a
//                 link would (src/save.c), or takes that back
// The key boxes have two name tabs, KEYS and PAD (both always reachable,
// rather than switching by the last device used, which confuses): the keyboard's keys or the controller's buttons are shown
// and set. Left/Right on any of the box's rows, or a tap on a tab,
// switches; DEFAULTS puts back the ones shown. The tab not shown is
// darker (ppu.c draws them).
// The boxes start at the GBA screen's line 64, where BUTTON CONFIG's box
// was (with JVPPBPORT_GBAVIEW there are none, and the GBA's box stays).
// The rows of all boxes are numbered together, in that order. On a row
// with a value (SCREEN's, TOUCH) Left/Right change it and A goes to the
// next one (wrapping round).
#include <stdio.h>
#include "jvppbport.h"

#define BOX_HEIGHT(rows) (28 + 12 * (rows))  // top, rows, bottom and the frame under it
#define FIRST_ROW_Y 18                        // the first row's text, from the box's top

#define PANEL_TOP 64                          // BUTTON CONFIG's box's top line
#define MAX_BOXES 4
#define SCREEN_NOTE "FILTERS LOOK BEST AT 'SNAP FILL' OR 2X+"

static const char *const sFilterNames[PORT_LCD_COUNT] = {"OFF", "LCD", "GBA"};

int PortPinballKeyRows(void)
{
    if (!gPortStaticView)
        return 0;
    return PortTouchEnabled() + PortPinballKeyCount() + 1;  // TOUCH, the actions, DEFAULTS
}

// The PC's own menu items: EXIT on the main menu, the SAVE AND QUIT key
// (not on touch screens: apps aren't closed there; not with
// JVPPBPORT_GBAVIEW).
int PortPcMenus(void)
{
    return gPortStaticView && !PortTouchEnabled();
}

int PortScreenRows(void)
{
    if (!gPortStaticView)
        return 0;  // JVPPBPORT_GBAVIEW: no room under the GBA screen
    return PortTouchEnabled() ? 1 : 3;
}

// SCREEN's rows: touch screens only have FILTER.
enum { SCREEN_FULLSCREEN, SCREEN_FILTER, SCREEN_SCALE };

static int ScreenItem(int i)
{
    return PortTouchEnabled() ? SCREEN_FILTER : i;
}

int PortMenuKeyRows(void)
{
    if (!gPortStaticView)
        return 0;
    return PortMenuKeyCount() + 1;  // and DEFAULTS
}

// E-READER's one row (not with JVPPBPORT_GBAVIEW).
static int EReaderRows(void)
{
    return gPortStaticView ? 1 : 0;
}

enum { BOX_PINBALL, BOX_SCREEN, BOX_MENU, BOX_EREADER };

static const char *const sBoxNames[MAX_BOXES] = {"PINBALL KEYS", "SCREEN", "MENU KEYS", "E-READER"};
static const char *const sPadBoxNames[MAX_BOXES] = {"PINBALL PAD", NULL, "MENU PAD", NULL};
#define TAB_TEXT_X 17  // the first tab's name (as BUTTON CONFIG's)
#define TAB_STEP 14    // from a tab's name's end to the next tab's name (past its slanted edge)

static int BoxRows(int kind)
{
    switch (kind)
    {
    case BOX_PINBALL:
        return PortPinballKeyRows();
    case BOX_SCREEN:
        return PortScreenRows();
    case BOX_MENU:
        return PortMenuKeyRows();
    default:
        return EReaderRows();
    }
}

int PortOptionsRows(void)
{
    return PortPinballKeyRows() + PortScreenRows() + PortMenuKeyRows() + EReaderRows();
}

int PortOptionsBoxCount(void)
{
    int kind, count = 0;

    for (kind = 0; kind < MAX_BOXES; kind++)
        count += BoxRows(kind) > 0;
    return count;
}

// The GBA screen line where the boxes start.
int PortOptionsPanelTop(void)
{
    return PANEL_TOP;
}

// The grey note under a box's rows (SCREEN's, when it has SCALE), or NULL.
static const char *NoteFor(int kind)
{
    return kind == BOX_SCREEN && !PortTouchEnabled() ? SCREEN_NOTE : NULL;
}

// Box b (of those shown): its kind, first row, number of rows and top line
// (GBA screen lines, going on under the screen).
static int FindBox(int b, int *firstRow, int *rows, int *top)
{
    int kind;

    *firstRow = 0;
    *top = PANEL_TOP;
    for (kind = 0; kind < MAX_BOXES; kind++)
    {
        *rows = BoxRows(kind);
        if (*rows == 0)
            continue;
        if (b-- == 0)
            return kind;
        *firstRow += *rows;
        *top += BOX_HEIGHT(*rows + (NoteFor(kind) != NULL));
    }
    return -1;
}

void PortOptionsBox(int b, const char **name, int *firstRow, int *rows, int *top)
{
    *name = sBoxNames[FindBox(b, firstRow, rows, top)];
}

// Box b's note (drawn as one more line under its rows), or NULL.
const char *PortOptionsBoxNote(int b)
{
    int first, rows, top;

    return NoteFor(FindBox(b, &first, &rows, &top));
}

// Box b's name tabs (1 or 2): their names, where their names start
// (GBA screen x) and which one is shown.
int PortOptionsBoxTabs(int b, const char *names[2], int textX[2], int *shown)
{
    int first, rows, top, kind = FindBox(b, &first, &rows, &top);

    names[0] = sBoxNames[kind];
    names[1] = sPadBoxNames[kind];
    textX[0] = TAB_TEXT_X;
    textX[1] = TAB_TEXT_X + PortBoldTextWidth(names[0]) + 1 + TAB_STEP;
    *shown = names[1] && PortPadTab(kind == BOX_MENU);
    return names[1] ? 2 : 1;
}

// Shows tab t of box b (a tap on it).
void PortOptionsSelectTab(int b, int t)
{
    int first, rows, top, kind = FindBox(b, &first, &rows, &top);

    if (kind == BOX_PINBALL || kind == BOX_MENU)
        PortSetPadTab(kind == BOX_MENU, t);
}

#define TOUCH_ROW (-1)  // PINBALL KEYS' TOUCH row (index -1: before the actions)

// Row r: its box's kind, and its place in that box (in PINBALL KEYS, the
// action; TOUCH_ROW for the TOUCH row).
static int RowKind(int row, int *index)
{
    int b, first, rows, top, kind;

    for (b = 0; (kind = FindBox(b, &first, &rows, &top)) >= 0; b++)
    {
        if (row < first + rows)
        {
            *index = row - first;
            if (kind == BOX_PINBALL && PortTouchEnabled())
                --*index;
            return kind;
        }
    }
    *index = 0;
    return -1;
}

// Lines of all the boxes, from PortOptionsPanelTop().
int PortOptionsPanelHeight(void)
{
    int b, height = 0, first, rows, top;
    const char *name;

    for (b = 0; b < PortOptionsBoxCount(); b++)
    {
        PortOptionsBox(b, &name, &first, &rows, &top);
        height += BOX_HEIGHT(rows + (PortOptionsBoxNote(b) != NULL));
    }
    return height;
}

// The GBA screen line where row r's text starts.
int PortOptionsRowY(int row)
{
    int b, first, rows, top;
    const char *name;

    for (b = 0; b < PortOptionsBoxCount(); b++)
    {
        PortOptionsBox(b, &name, &first, &rows, &top);
        if (row < first + rows)
            return top + FIRST_ROW_Y + 12 * (row - first);
    }
    return 0;
}

// Row r has a value between arrows (SCREEN's rows, TOUCH), not keys.
int PortOptionsRowHasArrows(int row)
{
    int i, kind = RowKind(row, &i);

    return kind == BOX_SCREEN || (kind == BOX_PINBALL && i == TOUCH_ROW);
}

// Where a key row's keys are written (after the longest label in its box).
int PortOptionsRowKeysX(int row)
{
    int i;

    return RowKind(row, &i) == BOX_PINBALL ? 128 : 96;
}

const char *PortOptionsRowLabel(int row)
{
    int i, kind = RowKind(row, &i);

    if (kind == BOX_PINBALL && i == TOUCH_ROW)
        return "TOUCH";
    if (kind == BOX_PINBALL)
        return i < PortPinballKeyCount() ? PortPinballKeyLabel(i) : "DEFAULTS";
    if (kind == BOX_MENU)
        return i < PortMenuKeyCount() ? PortMenuKeyLabel(i) : "DEFAULTS";
    if (kind == BOX_EREADER)
        return PortEReaderExclusivesShared() ? "LOCK E-READER EXCLUSIVES" : "UNLOCK E-READER EXCLUSIVES";
    return ScreenItem(i) == SCREEN_FULLSCREEN ? "FULLSCREEN" : ScreenItem(i) == SCREEN_FILTER ? "FILTER" : "SCALE";
}

const char *PortOptionsRowValue(int row)
{
    static char text[32];
    int i, kind = RowKind(row, &i);

    if (kind == BOX_PINBALL && i == TOUCH_ROW)
        return PortTouchControls() == PORT_TOUCH_BUTTONS ? "BUTTONS" : PortTouchControls() == PORT_TOUCH_ZONES ? "ZONES" : "OFF";
    if (kind != BOX_SCREEN)
    {
        text[0] = '\0';
        if (kind == BOX_PINBALL && i < PortPinballKeyCount())
            PortPinballKeyText(i, text, sizeof(text));
        else if (kind == BOX_MENU && i < PortMenuKeyCount())
            PortMenuKeyText(i, text, sizeof(text));
        return text;
    }
    if (ScreenItem(i) == SCREEN_FULLSCREEN)
        return PortFullscreen() ? "ON" : "OFF";
    if (ScreenItem(i) == SCREEN_FILTER)
        return sFilterNames[PortLcdFilter()];
    if (PortScaleMode() == 0)
        return "FILL";
    if (PortScaleMode() == PORT_SCALE_SNAP)
        return "SNAP FILL";
    snprintf(text, sizeof(text), "%dX", PortScaleMode());
    return text;
}

// Left (dir -1), Right (1) or A (0) on row r. Returns 1 if that did something.
int PortOptionsRowChange(int row, int dir)
{
    int count, value, i, kind = RowKind(row, &i);

    if (kind == BOX_PINBALL && i == TOUCH_ROW)
    {
        // BUTTONS, ZONES, OFF, round in that order (A goes on as Right does).
        static const int order[3] = {PORT_TOUCH_BUTTONS, PORT_TOUCH_ZONES, PORT_TOUCH_OFF};
        int at = PortTouchControls() == PORT_TOUCH_BUTTONS ? 0 : PortTouchControls() == PORT_TOUCH_ZONES ? 1 : 2;

        PortSetTouchControls(order[(at + (dir < 0 ? 2 : 1)) % 3]);
        return 1;
    }
    if (kind == BOX_EREADER)
    {
        if (dir != 0)
            return 0;  // A only
        PortSetEReaderExclusivesShared(!PortEReaderExclusivesShared());
        return 1;
    }
    if ((kind == BOX_PINBALL || kind == BOX_MENU) && dir != 0)
    {
        // Left/Right: the other tab (KEYS, PAD).
        PortSetPadTab(kind == BOX_MENU, !PortPadTab(kind == BOX_MENU));
        return 1;
    }
    if (kind == BOX_PINBALL)
    {
        if (i < PortPinballKeyCount())
            PortKeyCaptureStart(i);
        else
            PortPinballKeysDefault();
        return 1;
    }
    if (kind == BOX_MENU)
    {
        if (i < PortMenuKeyCount())
            PortMenuKeyCaptureStart(i);
        else
            PortMenuKeysDefault();
        return 1;
    }
    if (ScreenItem(i) == SCREEN_FULLSCREEN)
    {
        PortSetFullscreen(!PortFullscreen());  // two values: every step switches
        return 1;
    }
    if (dir == 0)
        dir = 1;
    if (ScreenItem(i) == SCREEN_FILTER)
    {
        count = PORT_LCD_COUNT;
        value = PortLcdFilter();
    }
    else
    {
        // In order: FILL, SNAP FILL, 1X ... the most that fits the screen.
        count = PortScaleMax() + 2;
        value = PortScaleMode() == PORT_SCALE_SNAP ? 1 : PortScaleMode() == 0 ? 0 : PortScaleMode() + 1;
        if (value >= count)
            value = count - 1;
    }
    value = (value + dir + count) % count;
    if (ScreenItem(i) == SCREEN_FILTER)
        PortSetLcdFilter(value);
    else
        PortSetScaleMode(value == 1 ? PORT_SCALE_SNAP : value == 0 ? 0 : value - 1);
    return 1;
}
