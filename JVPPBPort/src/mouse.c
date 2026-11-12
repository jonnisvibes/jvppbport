// JVPPBPort: the mouse in menus, also used for touch: fingers come
// here as mouse events (touch.c; a tap = moving there and clicking). On PC
// it was off for a while (2026-10-24: moving it across Options made the
// cursor follow it with a sound per item); back since 2026-10-25 with
// pointing silent: cursor moves made to follow the pointer don't play the
// game's cursor sound (gPortQuietMenuMove, src/m4a.c), clicks still sound,
// and keys, pads and the wheel move the cursor with the sound as before.
//
// - Pointing at a menu item moves the game's cursor there; a left click
//   moves it there and confirms (A). The cursor is moved by pressing the
//   game's own D-pad buttons, a frame at a time, until it gets there, so the
//   game reacts as to the keys (sounds, scrolling).
// - Some things are buttons instead: the Mart's arrows press Left/Right.
// - The Pokédex list can be dragged: a row up/down (the list follows the
//   pointer, the row under it is chosen; a press there that doesn't move
//   opens it when let go), or the long scroll bar's knob.
// - Right click = B (back), wheel = Up/Down (a sideways wheel = Left/Right).
//   On touch screens swipes come here as wheel notches (touch.c).
// - Screens without a description below: a left click anywhere = A.
//
// Items are rectangles on the GBA screen (ppu.c maps the canvas back to it,
// layouts included), or on the canvas for things only the tall view has
// (Field Select's table pictures).
//
// This file reads game structs, so it uses the game's GBA layout rule.
#include "jvppbport_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "main.h"
#include "titlescreen.h"
#include "m4a.h"
#include "constants/bg_music.h"
#include "constants/fields.h"
#include "constants/board/main_board.h"
#include "jvppbport.h"

#define MAX_TARGETS 40
#define STUCK_FRAMES 30  // give up moving the cursor if it doesn't move for this long
#define MAX_QUEUED 8

int gPortQuietMenuMove;  // this frame's cursor move follows the pointer: no SE_MENU_MOVE

struct Target
{
    s16 x, y, w, h;  // on the GBA screen, or on the canvas if `canvas`
    s16 item;        // the menu item (cursor value) it stands for, or -1
    u16 button;      // with item -1: the button a click presses
    u8 canvas;
    s8 tabBox, tab;  // or (tabBox >= 0) Options: a box's name tab, shown by a click
};

struct Screen
{
    int current;       // the cursor's item now; -1: no cursor
    u16 prev, next;    // buttons that move the cursor to a lower / higher item
    u16 (*step)(int current, int target);  // or this picks the button (cursors not in one line)
    u8 hold;           // the game moves the cursor while a button is held (Pokédex)
    u8 clickConfirms;  // a click that hits nothing presses A
    u8 busy;           // the cursor moves, but confirming has to wait (an animation)
    u8 noClicks;       // clicks do nothing here (they are being read as keys)
    u8 drag;           // the Pokédex list: rows and scroll bar can be dragged
    int first, last;   // the items the wheel can move to (default: those in `targets`)
    s16 *wait;         // the game's delay between cursor moves, skipped for the mouse
    int count;
    struct Target targets[MAX_TARGETS];
};

// Title menu (titlescreen.c), Pokédex list (pokedex.c).
extern const struct VectorU16 gTitleNoSaveSelectorPositions[4];
extern const struct VectorU16 gTitleSavedSelectorPositions[5];
extern struct TitlescreenStruct gTitlescreen;
extern s16 gPokedexListPosition;
extern s16 gPokedexSelectedMon;
extern s16 gPokedexListEntryCount;
extern s16 gPokedexScrollWaitFrames;
// Port functions in game files.
int PortOptionsState(int *cursor);
int PortFieldSelectState(int *ballSpeed);
int PortShopIsEvolution(void);
int PortFieldSelectChoice(int *ballSpeedShown);
void PortFieldSelectRect(int which, int *x, int *y, int *w, int *h);

#define TITLE_MENU_INPUT_NO_SAVED_GAME 4  // titlescreen.c's substates
#define TITLE_MENU_INPUT_SAVED_GAME 5
#define DEX_LIST_INPUT 1                  // pokedex.c's states
#define DEX_AFTER_SCROLL 2
#define DEX_ROW_H 16
#define OPTIONS_MAIN 0                    // options.c's states
#define OPTIONS_BUTTON_CONFIG_SELECT 3
#define OPTIONS_BUTTON_CONFIG_INPUT 4
#define OPTIONS_EDIT 6                    // cursor values: EDIT, then the five actions
#define OPTIONS_FIRST_ACTION 7
#define OPTIONS_RUMBLE_ON 13              // the RUMBLE column (Game Boy Player only: Android)
#define OPTIONS_RUMBLE_OFF 14
#define OPTIONS_SCREEN 15                 // the port's rows (PINBALL KEYS, SCREEN, MENU KEYS; screenmenu.c)
#define FIELD_SELECT_INPUT 1               // field_select.c's substate taking input
#define FIELD_SELECT_CHOOSE 0
#define FIELD_SELECT_SWITCHING 1  // sliding to the other table (Left/Right work, A doesn't)
#define FIELD_SELECT_BALL_SPEED 2

static void Add(struct Screen *s, int x, int y, int w, int h, int item, u16 button, int canvas)
{
    if (s->count < MAX_TARGETS)
        s->targets[s->count++] = (struct Target){x, y, w, h, item, button, canvas, -1, -1};
}

static void TitleMenu(struct Screen *s, const struct VectorU16 *positions, int count, int rowH)
{
    int i;

    s->current = gTitlescreen.menuCursorIndex;
    s->prev = DPAD_UP;
    s->next = DPAD_DOWN;
    s->clickConfirms = FALSE;
    for (i = 0; i < count; i++)
        Add(s, 64, positions[i].y - 3, 112, rowH, i, 0, FALSE);
    if (PortPcMenus())
        Add(s, 64, PortTitleExitY(count == 5) - 3, 112, rowH, count, 0, FALSE);  // EXIT (layouts.c)
}

extern int gGameBoyPlayerEnabled;

// The main column's order: BGM, SE, then the port's rows (in place of
// TYPE A-D and EDIT; those only with JVPPBPORT_GBAVIEW, without port rows).
static int OptionsRank(int item)
{
    return item >= OPTIONS_SCREEN ? item - OPTIONS_SCREEN + 2 : item;
}

// Options: Up/Down along the main column; with a RUMBLE column, Right goes
// from BGM / SE to RUMBLE ON / OFF, Left back, Up/Down within it.
static u16 OptionsStep(int current, int target)
{
    int inRumble = current == OPTIONS_RUMBLE_ON || current == OPTIONS_RUMBLE_OFF;
    int toRumble = target == OPTIONS_RUMBLE_ON || target == OPTIONS_RUMBLE_OFF;

    if (inRumble && toRumble)
        return target > current ? DPAD_DOWN : DPAD_UP;
    if (inRumble)
        return DPAD_LEFT;
    if (toRumble)
        return current <= 1 ? DPAD_RIGHT : DPAD_UP;
    return OptionsRank(target) > OptionsRank(current) ? DPAD_DOWN : DPAD_UP;
}

// The Options screen (gOptionsCursorPositionTable has the cursor's spots).
static void OptionsScreen(struct Screen *s)
{
    static const u8 sRowY[] = {32, 44, 84, 96, 108, 120, 132};
    int cursor, state = PortOptionsState(&cursor), i;

    s->prev = DPAD_UP;
    s->next = DPAD_DOWN;
    if (state == OPTIONS_MAIN)
    {
        s->current = cursor <= OPTIONS_EDIT || cursor >= OPTIONS_RUMBLE_ON ? cursor : -1;
        s->clickConfirms = FALSE;
        s->step = OptionsStep;
        if (PortKeyCapturing())
        {
            s->noClicks = TRUE;  // waiting for a key
            return;
        }
        // The port's rows (canvas places): a click on a row goes there and
        // presses A (sets a key, steps a SCREEN value); with the cursor on a
        // SCREEN row, its arrows step back and on.
        for (i = 0; i < PortOptionsRows(); i++)
        {
            int x, y;

            PortScreenToCanvas(16, PortOptionsRowY(i) - 3 - (PortOptionsRowH() - 12) / 2, &x, &y);
            if (cursor == OPTIONS_SCREEN + i && PortOptionsRowHasArrows(i))
            {
                // (Fingers get bigger targets: the row's whole value area.)
                if (PortTouchEnabled())
                {
                    Add(s, x + 60, y, 40, PortOptionsRowH(), -1, DPAD_LEFT, TRUE);
                    Add(s, x + 116, y, 44, PortOptionsRowH(), -1, DPAD_RIGHT, TRUE);
                }
                else
                {
                    Add(s, x + 78, y, 14, PortOptionsRowH(), -1, DPAD_LEFT, TRUE);
                    Add(s, x + 128, y, 14, PortOptionsRowH(), -1, DPAD_RIGHT, TRUE);
                }
            }
            Add(s, x, y, 216, PortOptionsRowH(), OPTIONS_SCREEN + i, 0, TRUE);
        }
        // The PINBALL and MENU boxes' name tabs (KEYS, PAD).
        for (i = 0; i < PortOptionsBoxCount(); i++)
        {
            const char *names[2];
            int textX[2], shown, t, tabs = PortOptionsBoxTabs(i, names, textX, &shown);
            int x, y, first, rows, top;

            PortOptionsBox(i, &names[0], &first, &rows, &top);
            for (t = 0; t < tabs && tabs > 1; t++)
            {
                PortScreenToCanvas(textX[t] - 4, top + 1, &x, &y);
                Add(s, x, y, PortBoldTextWidth(names[t]) + 12, 14, -1, 0, TRUE);
                s->targets[s->count - 1].tabBox = i;
                s->targets[s->count - 1].tab = t;
            }
        }
        s->first = 0;  // the wheel stays in the main column
        s->last = PortOptionsRows() > 0 ? 1 : OPTIONS_EDIT;
        for (i = 0; i < 2; i++)
            Add(s, 16, sRowY[i] - 2, 104, 12, i, 0, FALSE);
        if (gGameBoyPlayerEnabled == TRUE)
        {
            for (i = 0; i < 2; i++)
                Add(s, 124, sRowY[i] - 2, 108, 12, OPTIONS_RUMBLE_ON + i, 0, FALSE);
        }
        // TYPE A-D, EDIT (JVPPBPORT_GBAVIEW); the key column also leads to EDIT.
        for (i = 2; i <= OPTIONS_EDIT && PortOptionsRows() == 0; i++)
        {
            Add(s, 16, sRowY[i] - 2, 72, 12, i, 0, FALSE);
            Add(s, 88, sRowY[i] - 2, 144, 12, OPTIONS_EDIT, 0, FALSE);
        }
    }
    else if (state == OPTIONS_BUTTON_CONFIG_SELECT)
    {
        s->current = cursor;
        s->clickConfirms = FALSE;
        for (i = 0; i < 5; i++)
            Add(s, 88, sRowY[2 + i] - 2, 144, 12, OPTIONS_FIRST_ACTION + i, 0, FALSE);
    }
    else if (state == OPTIONS_BUTTON_CONFIG_INPUT)
    {
        s->clickConfirms = FALSE;  // waiting for keys (mouse buttons can be one)
        s->noClicks = TRUE;
    }
}

static void FieldSelect(struct Screen *s)
{
    int speed, state = PortFieldSelectState(&speed), field, chosen, x, y, w, h;

    if (!gPortStaticView)
        return;
    s->prev = DPAD_LEFT;
    s->next = DPAD_RIGHT;
    s->clickConfirms = FALSE;
    if (gMain.subState != FIELD_SELECT_INPUT)
        return;  // loading or leaving: the game ignores the buttons
    if (state == FIELD_SELECT_CHOOSE || state == FIELD_SELECT_SWITCHING)
    {
        s->busy = state == FIELD_SELECT_SWITCHING;
        s->current = PortFieldSelectChoice(&x);
        for (field = 0; field < MAIN_FIELD_COUNT; field++)
        {
            PortFieldSelectRect(field, &x, &y, &w, &h);
            Add(s, x, y, w, h, field, 0, TRUE);
        }
    }
    else if (state == FIELD_SELECT_BALL_SPEED)
    {
        // The other table: back to picking one. In the box: the arrows
        // switch the speed, the rest confirms.
        chosen = PortFieldSelectChoice(&x);
        PortFieldSelectRect(1 - chosen, &x, &y, &w, &h);
        Add(s, x, y, w, h, -1, B_BUTTON, TRUE);
        PortFieldSelectRect(2, &x, &y, &w, &h);
        Add(s, x + 13, y + 16, 18, 18, -1, DPAD_LEFT, TRUE);
        Add(s, x + 54, y + 16, 18, 18, -1, DPAD_RIGHT, TRUE);
        Add(s, x, y, w, h, -1, A_BUTTON, TRUE);
    }
}

// For touch.c's big arrows: Ball Speed is being chosen, and where its box is.
int PortFieldSpeedRect(int *x, int *y, int *w, int *h)
{
    int speed;

    if (!gPortStaticView || gMain.mainState != STATE_FIELD_SELECT || gMain.subState != FIELD_SELECT_INPUT
        || PortFieldSelectState(&speed) != FIELD_SELECT_BALL_SPEED)
        return FALSE;
    PortFieldSelectRect(2, x, y, w, h);
    return TRUE;
}

static void PokedexList(struct Screen *s)
{
    int rows = gPortStaticView ? PortDexListRows() : 5, i, x, y, w, h;

    s->current = gPokedexSelectedMon;
    s->prev = DPAD_UP;
    s->next = DPAD_DOWN;
    s->hold = TRUE;
    s->clickConfirms = FALSE;
    s->first = 0;
    s->last = gPokedexListEntryCount - 1;
    s->wait = &gPokedexScrollWaitFrames;
    s->drag = gPortStaticView;
    for (i = 0; i < rows && gPokedexListPosition + i < gPokedexListEntryCount; i++)
    {
        // Rows past the game's 5 are pictures: canvas places (layouts.c).
        if (gPortStaticView)
        {
            PortDexRowRect(i, &x, &y, &w, &h);
            Add(s, x, y, w, h, gPokedexListPosition + i, 0, TRUE);
        }
        else
            Add(s, 0, 80 + i * DEX_ROW_H, 160, DEX_ROW_H, gPokedexListPosition + i, 0, FALSE);
    }
}

static void PinballGame(struct Screen *s)
{
    if (gMain.modeChangeFlags & MODE_CHANGE_PAUSE)
    {
        // SAVE (0) and CANCEL (1).
        s->current = gCurrentPinballGame->pauseMenuCursorIndex;
        s->prev = DPAD_UP;
        s->next = DPAD_DOWN;
        s->clickConfirms = FALSE;
        if (PortTouchEnabled())
        {
            Add(s, 56, 58, 128, 24, 0, 0, FALSE);
            Add(s, 56, 82, 128, 24, 1, 0, FALSE);
        }
        else
        {
            Add(s, 88, 70, 80, 12, 0, 0, FALSE);
            Add(s, 88, 82, 80, 12, 1, 0, FALSE);
        }
    }
    else if (gMain.shopPanelActive)
    {
        // Pokémon Mart / Evolution Select: arrows beside the picture. In
        // Evolution Select, the EVO tag above it and the arrow under it
        // pick the stage (Up/Down).
        s->clickConfirms = FALSE;
        if (PortShopIsEvolution())
        {
            Add(s, 37, 62, 75, 20, -1, DPAD_UP, FALSE);
            Add(s, 37, 126, 75, 18, -1, DPAD_DOWN, FALSE);
        }
        // The arrows' targets are bigger than the arrows (fingers): from
        // the panel's left edge to the picture, and from it to the coins.
        Add(s, 0, 70, 37, 64, -1, DPAD_LEFT, FALSE);
        Add(s, 37, 80, 75, 46, -1, A_BUTTON, FALSE);
        Add(s, 112, 70, 30, 64, -1, DPAD_RIGHT, FALSE);
        // The button on the right (the Mart's price, Evolution Select's
        // name) picks, like the picture.
        Add(s, 142, 88, 90, 30, -1, A_BUTTON, FALSE);
        if (!PortShopIsEvolution())
        {
            // The Mart: the rest of its panel does nothing. With a mouse
            // a click anywhere else leaves it (B); touch screens have a
            // BACK button under the panel for that (touch.c).
            Add(s, 0, 14, PORT_SCREEN_WIDTH, 122, -1, 0, FALSE);
            if (!PortTouchEnabled())
                Add(s, -4096, -4096, 8192, 8192, -1, B_BUTTON, TRUE);
        }
    }
    else if (PortGameChoiceOpen())
    {
        // The boss hole's and travel's prompts: a picture over the center
        // hole (the same spot on both tables) with an A row (go) over a B
        // row (cancel). A click anywhere else is A, unless the left button
        // is a flipper (then only the rows count).
        int x = 95, y = 299, canvas = gPortStaticView;

        if (!canvas)
            x -= gCurrentPinballGame->cameraXOffset, y -= gCurrentPinballGame->cameraYOffset;
        if (PortTouchEnabled())
        {
            // Fingers: the rows reach past the little picture.
            Add(s, x - 40, y - 20, 128, 37, -1, A_BUTTON, canvas);
            Add(s, x - 40, y + 17, 128, 37, -1, B_BUTTON, canvas);
        }
        else
        {
            Add(s, x, y, 48, 17, -1, A_BUTTON, canvas);
            Add(s, x, y + 17, 48, 17, -1, B_BUTTON, canvas);
        }
    }
}

// A choice in a game where B means back: the Mart (B leaves it), and the
// yes/no prompts at the boss hole and for travel (B says no). Elsewhere in a
// game, back pauses (input.c).
int PortGameChoiceOpen(void)
{
    struct PinballGame *game = gCurrentPinballGame;

    if (gMain.mainState != STATE_GAME_MAIN || gMain.selectedField >= MAIN_FIELD_COUNT)
        return FALSE;
    if (gMain.shopPanelActive)
        return !PortShopIsEvolution();
    if (gMain.modeChangeFlags != MODE_CHANGE_NONE || (game->modeAnimTimer != 145 && game->modeAnimTimer != 146))
        return FALSE;
    return (game->boardState == MAIN_BOARD_STATE_BOSS_HOLE_ACTIVE && game->boardSubState == BOSS_HOLE_SUBSTATE_AWAIT_CONFIRMATION)
        || (game->boardState == MAIN_BOARD_STATE_TRAVEL_MODE && game->boardSubState == TRAVEL_SUBSTATE_CONFIRMATION_AND_TRAVEL_CUTSCENE);
}

// For touch.c's big buttons: PORT_SHOP_* for what the panel offers now, 0
// while it isn't taking input (sliding in, a prize picked).
int PortShopArrows(void)
{
    struct PinballGame *game = gCurrentPinballGame;

    if (gMain.mainState != STATE_GAME_MAIN || !gMain.shopPanelActive || gMain.shopPanelSlideOffset <= 19
        || game->shopEntryTimer > 144 || game->catchModeEventTimer != 0 || (gMain.modeChangeFlags & MODE_CHANGE_PAUSE))
        return 0;
    if (!PortShopIsEvolution())
        return game->prizeSelected ? 0 : PORT_SHOP_SIDE | PORT_SHOP_BACK;
    return (game->evolvablePartySize > 1 ? PORT_SHOP_SIDE : 0)
         | (game->evoChainPosition < game->evoChainMaxStage ? PORT_SHOP_UP : 0)
         | (game->evoChainPosition > 0 ? PORT_SHOP_DOWN : 0);
}

static void DescribeScreen(struct Screen *s)
{
    int i;

    memset(s, 0, sizeof(*s));
    s->current = -1;
    s->clickConfirms = TRUE;
    if (PortConfirmOpen())
    {
        // KEEP / UNDO, as menu items so hovering picks one and a click presses it.
        int x, y, w, h;

        s->current = PortConfirmSelected();
        s->prev = DPAD_LEFT;
        s->next = DPAD_RIGHT;
        s->clickConfirms = FALSE;
        for (i = 0; i < 2; i++)
        {
            PortConfirmButton(i, &x, &y, &w, &h);
            Add(s, x, y, w, h, i, 0, TRUE);
        }
        s->first = 0;
        s->last = 1;
        return;
    }
    switch (gMain.mainState)
    {
    case STATE_TITLE:
        if (gMain.subState == TITLE_MENU_INPUT_NO_SAVED_GAME)
            TitleMenu(s, gTitleNoSaveSelectorPositions, 4, 14);
        else if (gMain.subState == TITLE_MENU_INPUT_SAVED_GAME)
            TitleMenu(s, gTitleSavedSelectorPositions, 5, 13);
        break;
    case STATE_OPTIONS:
        OptionsScreen(s);
        break;
    case STATE_FIELD_SELECT:
        FieldSelect(s);
        break;
    case STATE_POKEDEX:
        if (gMain.subState == DEX_LIST_INPUT || gMain.subState == DEX_AFTER_SCROLL)
            PokedexList(s);
        break;
    case STATE_GAME_MAIN:
        PinballGame(s);
        break;
    }
    if (s->current >= 0 && s->last == 0)
    {
        s->first = s->current;
        s->last = s->current;
        for (i = 0; i < s->count; i++)
        {
            if (s->targets[i].item >= 0 && s->targets[i].item < s->first)
                s->first = s->targets[i].item;
            if (s->targets[i].item > s->last)
                s->last = s->targets[i].item;
        }
    }
}

// ---- Turning the mouse into button presses ----

static int sTarget = -1;  // item the cursor is being moved to
static int sConfirm;      // press A once it's there
static int sPointing;     // sTarget follows the pointer (hover, click), not the wheel
static int sStuck, sStuckTarget = -1, sLastCurrent;
static u16 sLastOut;
static u16 sQueue[MAX_QUEUED];
static int sQueued;

static void Queue(u16 button)
{
    if (sQueued < MAX_QUEUED)
        sQueue[sQueued++] = button;
}

static const struct Target *HitTarget(const struct Screen *s, const struct PortMouse *m)
{
    int i;

    for (i = 0; i < s->count; i++)
    {
        const struct Target *t = &s->targets[i];
        int x = t->canvas ? m->x : m->sx, y = t->canvas ? m->y : m->sy;
        if ((t->canvas ? m->inside : m->onScreen)
            && x >= t->x && x < t->x + t->w && y >= t->y && y < t->y + t->h)
            return t;
    }
    return NULL;
}

// Dragging the Pokédex list (Screen.drag). A press on a row waits to see if
// it moves (DRAG_START px) before it's a drag; let go before that, it's a
// click on the row. Returns TRUE while the mouse is dragging (no pointing).
#define DRAG_START 4
enum { DRAG_NONE, DRAG_PRESSED, DRAG_ROWS, DRAG_BAR };
static int sDrag, sDragY, sDragPosition, sDragItem;

static int ListDrag(const struct Screen *s, const struct PortMouse *m, int *clicks)
{
    int x, y, w, h, rows = PortDexListRows(), row, position;

    if (!s->drag)
    {
        sDrag = DRAG_NONE;
        return FALSE;
    }
    if (*clicks && m->inside)
    {
        const struct Target *hit = HitTarget(s, m);

        PortDexBarRect(&x, &y, &w, &h);
        if (m->x >= x && m->x < x + w && m->y >= y && m->y < y + h)
            sDrag = DRAG_BAR;
        else if (hit && hit->item >= 0)
            sDrag = DRAG_PRESSED, sDragItem = hit->item;
        else
            return FALSE;
        sDragY = m->y;
        sDragPosition = gPokedexListPosition;
        *clicks = 0;
    }
    if (sDrag == DRAG_NONE)
        return FALSE;
    if (!m->down)
    {
        if (sDrag == DRAG_PRESSED)
            sTarget = sDragItem, sConfirm = TRUE, sPointing = TRUE;
        sDrag = DRAG_NONE;
        return FALSE;
    }
    if (sDrag == DRAG_PRESSED && abs(m->y - sDragY) >= DRAG_START)
        sDrag = DRAG_ROWS;
    if (sDrag == DRAG_PRESSED)
        return FALSE;
    sTarget = -1;
    sConfirm = FALSE;
    if (sDrag == DRAG_BAR)
    {
        PortDexScrollTo(gPokedexListPosition, PortDexBarItem(m->y));
        return TRUE;
    }
    // Rows: the list moves a row per row height, rounded.
    PortDexRowRect(0, &x, &y, &w, &h);
    position = sDragPosition - (m->y - sDragY + (m->y > sDragY ? h / 2 : -h / 2)) / h;
    if (position > gPokedexListEntryCount - rows)
        position = gPokedexListEntryCount - rows;
    if (position < 0)
        position = 0;
    row = (m->y - y) / h;
    if (row < 0)
        row = 0;
    if (row > rows - 1)
        row = rows - 1;
    PortDexScrollTo(position, position + row);
    return TRUE;
}

// Touch: a finger that lands here grabs the Pokédex list (a row or the
// scroll bar), so it works like a held mouse button instead of swiping
// (canvas pixels): the list follows the finger, a tap still opens the row.
int PortMouseDragSpot(int cx, int cy)
{
    struct Screen s;
    struct PortMouse m;
    const struct Target *hit;
    int x, y, w, h;

    DescribeScreen(&s);
    if (!s.drag)
        return FALSE;
    PortDexBarRect(&x, &y, &w, &h);
    if (cx >= x && cx < x + w && cy >= y && cy < y + h)
        return TRUE;
    memset(&m, 0, sizeof(m));
    m.x = cx, m.y = cy, m.inside = TRUE;
    hit = HitTarget(&s, &m);
    return hit && hit->canvas && hit->item >= 0;
}

// The GBA buttons the mouse presses this frame. A button pressed on the
// frame before is let go for a frame first, so the game sees a new press
// (except while moving a cursor that moves while the button is held).
u16 PortMouseButtons(const struct PortMouse *m)
{
    struct Screen s;
    const struct Target *hit;
    u16 want = 0, out;
    int i, fromQueue = FALSE, confirming = FALSE, clicks, rightClicks;

    DescribeScreen(&s);
    hit = m->inside ? HitTarget(&s, m) : NULL;
    clicks = s.noClicks ? 0 : m->clicks;
    rightClicks = s.noClicks ? 0 : m->rightClicks;
    if (ListDrag(&s, m, &clicks))
        hit = NULL;

    if (m->keyPressed || s.current < 0)
    {
        sTarget = -1;  // the keys are in charge, or there's no cursor
        sConfirm = FALSE;
    }
    if (m->moved && hit && hit->item >= 0 && s.current >= 0)
        sTarget = hit->item, sPointing = TRUE;
    for (i = 0; i < clicks; i++)
    {
        if (hit && hit->item >= 0 && s.current >= 0)
        {
            sTarget = hit->item;
            sConfirm = TRUE;
            sPointing = TRUE;
        }
        else if (hit && hit->item < 0)
        {
            if (hit->tabBox >= 0)
            {
                PortOptionsSelectTab(hit->tabBox, hit->tab);
                m4aSongNumStart(SE_MENU_SELECT);
            }
            else if (hit->button)
                Queue(hit->button);
        }
        else if (!hit && s.clickConfirms && !m->clicksOnTargets)
            Queue(A_BUTTON);
    }
    for (i = 0; i < rightClicks; i++)
        Queue(B_BUTTON);
    // The wheel moves a cursor by items (wheel up = the item before), or
    // presses Up/Down where there is none.
    if (m->wheel && s.current >= 0)
    {
        sTarget = (sTarget >= 0 ? sTarget : s.current) - m->wheel;
        if (sTarget < s.first)
            sTarget = s.first;
        if (sTarget > s.last)
            sTarget = s.last;
        sConfirm = FALSE;
        sPointing = FALSE;
    }
    for (i = 0; i < m->wheel && s.current < 0; i++)
        Queue(DPAD_UP);
    for (i = 0; i > m->wheel && s.current < 0; i--)
        Queue(DPAD_DOWN);
    // The sideways wheel presses Left/Right (the keys take over the cursor).
    if (m->hwheel)
        sTarget = -1, sConfirm = FALSE;
    for (i = 0; i < m->hwheel; i++)
        Queue(DPAD_RIGHT);
    for (i = 0; i > m->hwheel; i--)
        Queue(DPAD_LEFT);

    if (sQueued)
    {
        want = sQueue[0];
        fromQueue = TRUE;
    }
    else if (sTarget >= 0)
    {
        if (s.current == sTarget && s.busy)
        {
            want = 0;  // there; confirm once the animation is over
        }
        else if (s.current == sTarget)
        {
            sTarget = -1;
            want = sConfirm ? A_BUTTON : 0;
            confirming = sConfirm;
        }
        else
        {
            // Counted per target and cursor place. (It once stayed over the
            // limit after one move gave up, so every later target on a
            // cursor at the same number, e.g. on the next screen, was given
            // up at once: the mouse seemed dead on the title menu.)
            if (s.current != sLastCurrent || sTarget != sStuckTarget)
                sStuck = 0;
            sStuckTarget = sTarget;
            // Given up: the next try starts counting again, even for the
            // same target and cursor place (else, after a try made while
            // a screen was still loading, pointing there again was given
            // up at once until the cursor moved some other way).
            if (++sStuck > STUCK_FRAMES)
                sTarget = -1, sConfirm = FALSE, sStuckTarget = -1;
            else
                want = s.step ? s.step(s.current, sTarget) : sTarget > s.current ? s.next : s.prev;
            if (s.wait)
                *s.wait = 0;
        }
    }

    out = want;
    if (want && want == sLastOut && !(s.hold && (want & DPAD_ANY)))
        out = 0;  // let go first; try again next frame
    else if (fromQueue)
        memmove(sQueue, sQueue + 1, --sQueued * sizeof(sQueue[0]));
    else if (confirming)
        sConfirm = FALSE;
    if (!out && confirming)
        sTarget = s.current;  // still to confirm, next frame
    gPortQuietMenuMove = sPointing && !fromQueue && (out & DPAD_ANY);
    if (getenv("JVPPBPORT_MOUSE_TRACE") && (out || want || m->clicks))
        fprintf(stderr, "mouse: state %d/%d cur %d target %d want %x out %x hit %p at %d,%d screen %d,%d%s\n", gMain.mainState, gMain.subState, s.current, sTarget, want, out, (void *)hit, m->x, m->y, m->sx, m->sy, m->onScreen ? "" : " (off)");
    sLastCurrent = s.current;
    sLastOut = out;
    return out;
}
