// JVPPBPort: the boxes the port puts on the Options screen in place of the
// GBA's BUTTON CONFIG box and under it (drawn by ppu.c, worked by
// src/options.c and mouse.c):
//   PINBALL KEYS (in place of BUTTON CONFIG's TYPE A-D and EDIT: one set
//   of keys, each action free to change)
//     TOUCH   < BUTTONS / PLAIN / ZONES / OFF >   touch screens only: the
//                            on-screen flipper and pause buttons (PLAIN: the
//                            same without flipper pictures), or the screen's
//                            halves as flippers, or neither (touch.c)
//     LEFT FLIPPER ... TILT UP   the keys of each pinball action (input.c); A sets them
//     DEFAULTS    A puts the default keys back
//   SCREEN
//     FULLSCREEN < OFF / ON >          borderless fullscreen, as Alt+Enter or
//                                        F11 (platform.c; not on touch screens)
//     TATE MODE < OFF / ON / REVERSE >   the picture turned for a monitor on
//                                        its side (platform.c; not on touch screens)
//     FILTER  < OFF / LCD / GBA >        the LCD filter (lcd.c)
//     SCALE   < FILL / SNAP FILL / 1X / 2X ... >  the picture's scale (platform.c, touch.c)
//     BONUS STAGE < ORIGINAL / FULL >    the GBA's moving window or the whole stage (layouts.c)
//     (a note under SCALE: filters look best at whole-number scales)
//   MENU KEYS
//     UP ... R    the keys of each menu action (input.c); A sets them
//     SAVE AND QUIT  (PC) saves a game in progress and closes the program
//     DEFAULTS    A puts the default keys back
//     CONFIRM CHANGES   puts SCREEN's choices to use. Until then Left/Right
//                                        only change what's shown; leaving asks
//                                        SAVE CHANGES? A change to the picture
//                                        then asks KEEP CHANGES? and goes back
//                                        by itself without an answer (confirm.c).
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

#define BOX_HEIGHT(rows) (24 + PortOptionsRowH() * (rows))  // top, rows, bottom and the frame under it
#define FIRST_ROW_Y 18                        // the first row's text, from the box's top

#define PANEL_TOP 64                          // BUTTON CONFIG's box's top line
#define MAX_BOXES 4
#define SCREEN_NOTE "FILTERS LOOK BEST AT 'SNAP FILL' OR 2X+"

static const char *const sFilterNames[PORT_LCD_COUNT] = {"OFF", "LCD", "GBA"};

// Lines per row: 11 on PC, where all the boxes have to fit the 448-line
// canvas; roomy on touch screens, for fingers (the screen then scrolls, ppu.c).
int PortOptionsRowH(void)
{
    return PortTouchEnabled() ? 18 : 11;
}

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
    return PortTouchEnabled() ? 4 : 6;
}

// SCREEN's rows: touch screens start at FILTER.
enum { SCREEN_FULLSCREEN, SCREEN_TATE, SCREEN_FILTER, SCREEN_SCALE, SCREEN_BONUS, SCREEN_CONFIRM };

static int ScreenItem(int i)
{
    return PortTouchEnabled() ? SCREEN_FILTER + i : i;
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
    return kind == BOX_SCREEN ? SCREEN_NOTE : NULL;
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
            // A box's note goes before its last row (CONFIRM CHANGES stays last).
            return top + FIRST_ROW_Y + (PortOptionsRowH() - 12) / 2 + PortOptionsRowH() * (row - first + (PortOptionsBoxNote(b) && row == first + rows - 1));
    }
    return 0;
}

// Row r has a value between arrows (SCREEN's rows, TOUCH), not keys.
int PortOptionsRowHasArrows(int row)
{
    int i, kind = RowKind(row, &i);

    return (kind == BOX_SCREEN && ScreenItem(i) != SCREEN_CONFIRM) || (kind == BOX_PINBALL && i == TOUCH_ROW);
}

// Where a key row's keys are written (after the longest label in its box).
int PortOptionsRowKeysX(int row)
{
    int i;

    return RowKind(row, &i) == BOX_PINBALL ? 128 : 96;
}

// SCREEN's rows show what's being chosen; it's put to use by CONFIRM CHANGES
// (or SAVE on leaving).
static struct { int fullscreen, tate, filter, scale, bonus; } sPending;
static int sEdited;

static void PendingRead(void)
{
    if (sEdited)
        return;
    sPending.fullscreen = PortFullscreen();
    sPending.tate = PortTate();
    sPending.filter = PortLcdFilter();
    sPending.scale = PortScaleMode();
    sPending.bonus = PortBonusFull();
}

static int PictureEdited(void)
{
    return sPending.fullscreen != PortFullscreen() || sPending.tate != PortTate()
        || sPending.filter != PortLcdFilter() || sPending.scale != PortScaleMode();
}

int PortOptionsEdited(void)
{
    return sEdited;
}

void PortOptionsDiscard(void)
{
    sEdited = 0;
}

// Puts the choices to use. Returns 1 if the KEEP CHANGES? window opened.
int PortOptionsApply(int leaveOnKeep)
{
    int ask;

    if (!sEdited)
        return 0;
    ask = PictureEdited();
    sEdited = 0;
    if (ask)
        PortConfirmBegin(leaveOnKeep);
    PortSetFullscreen(sPending.fullscreen);
    PortSetTate(sPending.tate);
    PortSetScaleMode(sPending.scale);
    PortSetLcdFilter(sPending.filter);
    if (sPending.bonus != PortBonusFull())
        PortSetBonusFull(sPending.bonus);
    return ask;
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
    switch (ScreenItem(i))
    {
    case SCREEN_FULLSCREEN:
        return "FULLSCREEN";
    case SCREEN_TATE:
        return "TATE MODE";
    case SCREEN_FILTER:
        return "FILTER";
    case SCREEN_BONUS:
        return "BONUS STAGE";
    case SCREEN_CONFIRM:
        return "CONFIRM CHANGES";
    }
    return "SCALE";
}

const char *PortOptionsRowValue(int row)
{
    static char text[32];
    int i, kind = RowKind(row, &i);

    if (kind == BOX_PINBALL && i == TOUCH_ROW)
    {
        switch (PortTouchControls())
        {
        case PORT_TOUCH_BUTTONS:
            return "BUTTONS";
        case PORT_TOUCH_PLAIN:
            return "PLAIN";
        case PORT_TOUCH_ZONES:
            return "ZONES";
        }
        return "OFF";
    }
    if (kind != BOX_SCREEN)
    {
        text[0] = '\0';
        if (kind == BOX_PINBALL && i < PortPinballKeyCount())
            PortPinballKeyText(i, text, sizeof(text));
        else if (kind == BOX_MENU && i < PortMenuKeyCount())
            PortMenuKeyText(i, text, sizeof(text));
        return text;
    }
    PendingRead();
    switch (ScreenItem(i))
    {
    case SCREEN_FULLSCREEN:
        return sPending.fullscreen ? "ON" : "OFF";
    case SCREEN_TATE:
        return sPending.tate == PORT_TATE_ON ? "ON" : sPending.tate == PORT_TATE_REVERSE ? "REVERSE" : "OFF";
    case SCREEN_FILTER:
        return sFilterNames[sPending.filter];
    case SCREEN_BONUS:
        return sPending.bonus ? "FULL" : "ORIGINAL";
    case SCREEN_CONFIRM:
        return "";
    }
    if (sPending.scale == 0)
        return "FILL";
    if (sPending.scale == PORT_SCALE_SNAP)
        return "SNAP FILL";
    snprintf(text, sizeof(text), "%dX", sPending.scale);
    return text;
}

// Left (dir -1), Right (1) or A (0) on row r. Returns 1 if that did something.
int PortOptionsRowChange(int row, int dir)
{
    int count, value, i, kind = RowKind(row, &i);

    if (kind == BOX_PINBALL && i == TOUCH_ROW)
    {
        // BUTTONS, PLAIN, ZONES, OFF, round in that order (A goes on as Right does).
        static const int order[4] = {PORT_TOUCH_BUTTONS, PORT_TOUCH_PLAIN, PORT_TOUCH_ZONES, PORT_TOUCH_OFF};
        int at = 0;

        while (at < 3 && order[at] != PortTouchControls())
            at++;
        PortSetTouchControls(order[(at + (dir < 0 ? 3 : 1)) % 4]);
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
    if (ScreenItem(i) == SCREEN_CONFIRM)
    {
        if (dir != 0 || !sEdited)
            return 0;
        PortOptionsApply(0);
        return 1;
    }
    PendingRead();
    if (dir == 0)
        dir = 1;
    switch (ScreenItem(i))
    {
    case SCREEN_FULLSCREEN:
        sPending.fullscreen = !sPending.fullscreen;
        break;
    case SCREEN_TATE:
        sPending.tate = (sPending.tate + dir + PORT_TATE_COUNT) % PORT_TATE_COUNT;
        break;
    case SCREEN_BONUS:
        sPending.bonus = !sPending.bonus;
        break;
    case SCREEN_FILTER:
        sPending.filter = (sPending.filter + dir + PORT_LCD_COUNT) % PORT_LCD_COUNT;
        break;
    default:
    {
        // In order: FILL, SNAP FILL, then the whole-number sizes that fit.
        int min = PortScaleMin(), max = PortScaleMax();

        count = 2 + (max >= min ? max - min + 1 : 0);
        value = sPending.scale == PORT_SCALE_SNAP ? 1 : sPending.scale == 0 ? 0 : 2 + sPending.scale - min;
        if (value < 0)
            value = 2;
        if (value >= count)
            value = count - 1;
        value = (value + dir + count) % count;
        sPending.scale = value == 1 ? PORT_SCALE_SNAP : value == 0 ? 0 : value - 2 + min;
        break;
    }
    }
    sEdited = PictureEdited() || sPending.bonus != PortBonusFull();
    return 1;
}

// A SCREEN row whose shown value isn't in use yet (drawn yellow).
int PortOptionsRowEdited(int row)
{
    int i, kind = RowKind(row, &i);

    if (kind != BOX_SCREEN || !sEdited)
        return 0;
    switch (ScreenItem(i))
    {
    case SCREEN_FULLSCREEN:
        return sPending.fullscreen != PortFullscreen();
    case SCREEN_TATE:
        return sPending.tate != PortTate();
    case SCREEN_FILTER:
        return sPending.filter != PortLcdFilter();
    case SCREEN_SCALE:
        return sPending.scale != PortScaleMode();
    case SCREEN_BONUS:
        return sPending.bonus != PortBonusFull();
    }
    return 0;
}

// CONFIRM CHANGES with nothing to confirm (drawn grey).
int PortOptionsRowDim(int row)
{
    int i, kind = RowKind(row, &i);

    return kind == BOX_SCREEN && ScreenItem(i) == SCREEN_CONFIRM && !sEdited;
}
