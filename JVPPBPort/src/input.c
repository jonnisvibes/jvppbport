// JVPPBPort: keyboard and mouse.
//
// Menus and pinball have separate controls:
// - Menu keys press the GBA's buttons, on every screen including the menus
//   inside a game (pause, Evolution Select, Mart, prompts):
//     arrows = D-pad, Enter = A (confirm), Esc = B (back), but Start while
//     playing (pause; pressed again it resumes), Tab = Select (Pokédex animation),
//     [ and ] = L and R (only the game's secret key combos use them).
//     In the Mart and the yes/no prompts of a game (boss, travel), back is B.
//     Alt+Enter and F11 switch fullscreen (platform.c), and aren't game keys.
//   They can be changed on the Options screen (the port's MENU KEYS box,
//   screenmenu.c), up to two keys each, kept in
//   jvppbport.ini [MenuKeys]. A key can only do one menu action: setting it
//   for one takes it from the others. Esc always cancels setting a key, and
//   the DEFAULTS row puts the keys above back.
// - Pinball keys (flippers, tilts, LAUNCH: by default the right flipper's keys)
//   don't press GBA buttons: the game's pinball actions are read from them
//   directly (UpdateButtonActionsFromJoy in all_board_pinball_game_main.c
//   calls PortPinballActionHeld). There is one set of them, changed on the
//   Options screen's PINBALL KEYS box (screenmenu.c; it replaces the GBA's
//   BUTTON CONFIG with its TYPE A-D and EDIT),
//   and DEFAULTS puts back the old TYPE A keys. Each action has up to two
//   keys; either one works. They are kept in a settings file next to the save
//   file (jvppbport.ini [PinballKeys]; a test save x.sav gets x.ini), in plain
//   text, so the save file itself stays exactly the GBA's. Until they are
//   first changed, the keys of the TYPE chosen in the save (sPresets, or the
//   old [EditKeys] for EDIT) are used, so older setups carry on.
// - Game controllers (pad.c): every action
//   also has two controller slots (buttons, stick directions, triggers),
//   kept as left_flipper_pad / left_flipper_pad_2 etc. in the same sections
//   (with pad=1). Defaults: menus D-pad or left stick, A = OK, B or Start =
//   BACK, Back/Select = SELECT, LB/RB = L/R; pinball as the GBA's TYPE A
//   (D-pad Left, the right face button, LB, RB, LB+RB). While playing, a
//   key or button bound to a pinball action does nothing as a menu key
//   (so the right flipper, B, doesn't pause). The PINBALL and MENU boxes on the Options
//   screen have a KEYS and a PAD tab each (Left/Right on a row, or a tap on
//   the tab, switches; Android starts on PAD); setting a key takes a key or
//   a controller button, as the tab shown, and Start (like Esc, or Android's
//   Back) cancels. Keyboards still work on Android.
// - The mouse works the menus (mouse.c): pointing moves the cursor (silently),
//   left click = A, right click = B. Touch screens' fingers come to the same
//   code as mouse events (touch.c). The left and right mouse buttons
//   can be bound to pinball actions (click instead of pressing a key). They
//   count as two extra keys (MOUSE_LEFT, MOUSE_RIGHT).
//
// Test mode (see also platform.c):
//   JVPPBPORT_PRESS=400:START,700:A   hold a GBA button for 6 frames from that frame;
//                                     KEY_<name> holds a keyboard key instead (SDL's
//                                     key name, _ for spaces: KEY_Z, KEY_Right_Shift;
//                                     KEY_Mouse_Left / KEY_Mouse_Right hold a mouse button)
//   JVPPBPORT_HOLD_EVERY=40:A         also press a GBA button every 40 frames
//   JVPPBPORT_MOUSE=900:move:120:300,910:click,920:rclick,930:wheel:-2,940:hwheel:1
//                                     (menus only with JVPPBPORT_TOUCH, as fingers)
//                                     mouse at canvas position / left click / right
//                                     click / wheel notches (+ = up) / sideways
//                                     wheel notches (+ = right), at those frames
//                                     (down / up: left button pressed / let go, for drags)
//   JVPPBPORT_MOUSE_TRACE=1           print what the mouse presses (mouse.c)
// Scripted GBA buttons also work the pinball actions, through the game's own
// GBA button setup (A = right flipper etc.), so older test scripts still play.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <SDL.h>
#include "global.h"
#include "main.h"
#include "m4a.h"
#include "constants/pinball_inputs.h"
#include "constants/bg_music.h"
#include "jvppbport.h"

#define NO_KEY SDL_SCANCODE_UNKNOWN
#define TYPE_EDIT 4          // the game's TYPE A-D and EDIT (older setups)
#define SECOND_KEY_FRAMES 90 // after the first key, time to press an optional second one
// The mouse buttons as keys, in two spare places at the end of SDL's list.
#define MOUSE_LEFT ((SDL_Scancode)(SDL_NUM_SCANCODES - 2))
#define MOUSE_RIGHT ((SDL_Scancode)(SDL_NUM_SCANCODES - 1))
#define NUM_REAL_KEYS (SDL_NUM_SCANCODES - 2)
// Then the game controllers' buttons, sticks and triggers (pad.c).
#define PAD(code) ((SDL_Scancode)(PORT_PAD_BASE + (code)))
#define NUM_CODES (PORT_PAD_BASE + PORT_PAD_CODES)
#define IS_PAD(key) ((key) >= PORT_PAD_BASE)
// Each action has two keyboard keys (slots 0, 1) and two controller
// buttons (2, 3).
#define SLOTS 4
#define PAD_SLOT 2
#define PAD_START PAD(SDL_CONTROLLER_BUTTON_START)  // like Esc: cancels setting a key

// Menu actions: their names (Options, settings file), GBA buttons (BACK is
// B or Start, see BackButton) and default keys.
// SAVE AND QUIT (PC only; no key until one is
// set) saves a game in progress and closes the program.
enum { MENU_UP, MENU_DOWN, MENU_LEFT, MENU_RIGHT, MENU_OK, MENU_BACK, MENU_SELECT, MENU_L, MENU_R, MENU_QUIT, NUM_MENU_ACTIONS };
// Controller: D-pad or left stick, A = OK, B or Start = BACK (Start pauses
// while playing; B is the right flipper then, see MenuHeld), Back/Select =
// SELECT, LB and RB = L and R (the game's own combos use them: hold L +
// Left and press R three times on the title screen to delete the save).
static const struct
{
    const char *label, *setting;
    u16 button;
    SDL_Scancode keys[SLOTS];
} sMenuActions[NUM_MENU_ACTIONS] = {
    {"UP", "up", DPAD_UP, {SDL_SCANCODE_UP, 0, PAD(SDL_CONTROLLER_BUTTON_DPAD_UP), PAD(PORT_PAD_LY_NEG)}},
    {"DOWN", "down", DPAD_DOWN, {SDL_SCANCODE_DOWN, 0, PAD(SDL_CONTROLLER_BUTTON_DPAD_DOWN), PAD(PORT_PAD_LY_POS)}},
    {"LEFT", "left", DPAD_LEFT, {SDL_SCANCODE_LEFT, 0, PAD(SDL_CONTROLLER_BUTTON_DPAD_LEFT), PAD(PORT_PAD_LX_NEG)}},
    {"RIGHT", "right", DPAD_RIGHT, {SDL_SCANCODE_RIGHT, 0, PAD(SDL_CONTROLLER_BUTTON_DPAD_RIGHT), PAD(PORT_PAD_LX_POS)}},
    {"OK", "ok", A_BUTTON, {SDL_SCANCODE_RETURN, 0, PAD(SDL_CONTROLLER_BUTTON_A)}},
    {"BACK", "back", 0, {SDL_SCANCODE_ESCAPE, 0, PAD(SDL_CONTROLLER_BUTTON_B), PAD_START}},
    {"SELECT", "select", SELECT_BUTTON, {SDL_SCANCODE_TAB, 0, PAD(SDL_CONTROLLER_BUTTON_BACK)}},
    {"L", "l", L_BUTTON, {SDL_SCANCODE_LEFTBRACKET, 0, PAD(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)}},
    {"R", "r", R_BUTTON, {SDL_SCANCODE_RIGHTBRACKET, 0, PAD(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)}},
    {"SAVE AND QUIT", "save_and_quit", 0, {0}},
};
static SDL_Scancode sMenuKeys[NUM_MENU_ACTIONS][SLOTS];

// Pinball keys of the old TYPE A-D layouts, in the game's action order: left
// flipper, right flipper (and launch), tilt left, tilt right, tilt up. TYPE A
// is the default.
static const SDL_Scancode sPresets[TYPE_EDIT][NUM_PINBALL_INPUTS][2] = {
    // TYPE A, Space Cadet style (the keys the port has used so far).
    {{SDL_SCANCODE_Z}, {SDL_SCANCODE_SLASH}, {SDL_SCANCODE_X}, {SDL_SCANCODE_PERIOD}, {SDL_SCANCODE_UP}},
    // TYPE B, Shift keys for the flippers, Ctrl keys to tilt.
    {{SDL_SCANCODE_LSHIFT}, {SDL_SCANCODE_RSHIFT, SDL_SCANCODE_SPACE}, {SDL_SCANCODE_LCTRL}, {SDL_SCANCODE_RCTRL}, {SDL_SCANCODE_UP}},
    // TYPE C, arrow keys (Down launches too).
    {{SDL_SCANCODE_LEFT}, {SDL_SCANCODE_RIGHT, SDL_SCANCODE_DOWN}, {SDL_SCANCODE_Z}, {SDL_SCANCODE_X}, {SDL_SCANCODE_UP}},
    // TYPE D, WASD style.
    {{SDL_SCANCODE_A}, {SDL_SCANCODE_D, SDL_SCANCODE_SPACE}, {SDL_SCANCODE_Q}, {SDL_SCANCODE_E}, {SDL_SCANCODE_W}},
};

// The game's five pinball actions, then the port's LAUNCH (its own keys,
// apart from the right flipper's; by default the same ones). The game reads
// it through gPortLaunch* (all_board_pinball_game_main.c).
#define LAUNCH PORT_PINBALL_LAUNCH
#define NUM_ACTIONS (NUM_PINBALL_INPUTS + 1)
static const char *const sActionNames[NUM_ACTIONS] = {
    "left_flipper", "right_flipper", "tilt_left", "tilt_right", "tilt_up", "launch",
};
static const char *const sActionLabels[NUM_ACTIONS] = {
    "LEFT FLIPPER", "RIGHT FLIPPER", "TILT LEFT", "TILT RIGHT", "TILT UP", "LAUNCH",
};

// The controller's pinball buttons: the GBA's TYPE A on a pad: left flipper D-pad Left, right flipper the right face button
// (B on Xbox, Circle, A on Switch: buttons go by place, pad.c), tilt left
// LB, tilt right RB, tilt up LB+RB together (both tilts happen too, as on
// the GBA).
static const SDL_Scancode sPadPinball[NUM_PINBALL_INPUTS][2] = {
    {PAD(SDL_CONTROLLER_BUTTON_DPAD_LEFT)},
    {PAD(SDL_CONTROLLER_BUTTON_B)},
    {PAD(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)},
    {PAD(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)},
    {PAD(PORT_PAD_LB_RB)},
};

static SDL_Scancode sPinballKeys[NUM_ACTIONS][SLOTS];
static int sPinballSaved;  // [PinballKeys] is in the settings file
static SDL_Scancode sOldEditKeys[NUM_ACTIONS][SLOTS];  // [EditKeys], if any
static int sOldEditSaved;

static u8 sKeys[NUM_CODES];      // held this frame (keyboard, mouse, controllers, test script)
static u8 sLastKeys[NUM_CODES];  // held the frame before
static u8 sIgnore[NUM_CODES];    // held when a key was set: no menu action until let go
// The PINBALL and MENU boxes on the Options screen each have a KEYS and a
// PAD tab (sPadTab[menu]): which one shows (and is set). PAD at first on
// Android, where a keyboard is rare.
#ifdef __ANDROID__
static int sPadTab[2] = {TRUE, TRUE};
#else
static int sPadTab[2];
#endif
static u16 sScripted;                    // GBA buttons held by the test script
static u16 sBackButton;                  // what BACK presses, chosen when it goes down
static int sLastBack;
static int sLastQuit, sQuitWait;

static struct
{
    int active;
    int menu;  // setting a menu action's keys (else a pinball action's)
    int action;
    int count;
    int timer;
    int pad;  // the keys pressed are on a controller
    SDL_Scancode keys[2];
} sCapture;
static bool8 sCancelCapture;  // Android's Back: cancels setting a key

static struct PortMouse sMouse;
static int sMouseClicks, sMouseRightClicks, sMouseWheel, sMouseHWheel, sMouseMoved;
static u8 sMouseDown[2];     // left, right button held now
static u8 sMousePressed[2];  // went down since the last frame (a quick click still counts)

static const char *sPresses, *sHoldEvery, *sMouseScript;
// Android's Back button or gesture works like Esc. It goes down and up at
// once, so it's kept until the next frame reads it.
static bool8 sBackPressed;

// Key names as in the settings file and test scripts (SDL's, plus the mouse).
static const char *ScancodeName(SDL_Scancode key)
{
    static char pad[40];

    if (IS_PAD(key))
    {
        snprintf(pad, sizeof(pad), "Pad %s", PortPadCodeName(key - PORT_PAD_BASE) ? PortPadCodeName(key - PORT_PAD_BASE) : "?");
        return pad;
    }
    if (key == MOUSE_LEFT)
        return "Mouse Left";
    if (key == MOUSE_RIGHT)
        return "Mouse Right";
    return SDL_GetScancodeName(key);
}

static SDL_Scancode ScancodeFromName(const char *name)
{
    if (SDL_strncasecmp(name, "Pad ", 4) == 0)
        return PortPadCodeFromName(name + 4) >= 0 ? PAD(PortPadCodeFromName(name + 4)) : NO_KEY;
    if (SDL_strcasecmp(name, "Mouse Left") == 0)
        return MOUSE_LEFT;
    if (SDL_strcasecmp(name, "Mouse Right") == 0)
        return MOUSE_RIGHT;
    return SDL_GetScancodeFromName(name);
}

// Is a mouse button (MOUSE_LEFT/RIGHT) one of the pinball keys?
static int MouseBound(SDL_Scancode button)
{
    int i;

    for (i = 0; i < NUM_ACTIONS * SLOTS; i++)
    {
        if (sPinballKeys[i / SLOTS][i % SLOTS] == button)
            return TRUE;
    }
    return FALSE;
}

// ---- Settings file ([PinballKeys], [MenuKeys] in jvppbport.ini, see settings.c) ----

// An action's slot in the file: "left_flipper", "left_flipper_2" (keys),
// "left_flipper_pad", "left_flipper_pad_2" (controller).
static void SlotKey(char *out, size_t size, const char *action, int slot)
{
    static const char *const suffix[SLOTS] = {"", "_2", "_pad", "_pad_2"};

    snprintf(out, size, "%s%s", action, suffix[slot]);
}

static const char *PinballSetting(int action)
{
    return sActionNames[action];
}

static const char *MenuSetting(int action)
{
    return sMenuActions[action].setting;
}

// A section's keys; `pad` = PAD_DEFAULTS_VERSION is written so the file
// shows the controller's have been saved too (older files have only the
// keys). Version 1 files hold the first controller defaults, saved along
// with the keys, never chosen; they get the current defaults.
#define PAD_DEFAULTS_VERSION "2"
static void SaveKeys(const char *section, const char *(*name)(int), int count, const SDL_Scancode *keys)
{
    char key[40];
    int i, k;

    for (i = 0; i < count; i++)
    {
        for (k = 0; k < SLOTS; k++)
        {
            SlotKey(key, sizeof(key), name(i), k);
            if (keys[i * SLOTS + k] != NO_KEY)
                PortSettingSet(section, key, ScancodeName(keys[i * SLOTS + k]));
        }
    }
    PortSettingSet(section, "pad", PAD_DEFAULTS_VERSION);
}

// [PinballKeys] (the older [EditKeys] isn't needed after that).
static void SaveConfig(void)
{
    PortSettingsClearSection("EditKeys");
    PortSettingsClearSection("PinballKeys");
    SaveKeys("PinballKeys", PinballSetting, NUM_ACTIONS, &sPinballKeys[0][0]);
    // LAUNCH has been saved (files from before it get the right flipper's keys).
    PortSettingSet("PinballKeys", "launch_set", "1");
    // Written even with no keys at all, so the file shows they were set.
    PortSettingSet("PinballKeys", "set", "1");
    sPinballSaved = TRUE;
    PortSettingsSave();
}

// [MenuKeys]: every action's keys (none written: it has none).
static void SaveMenuKeys(void)
{
    PortSettingsClearSection("MenuKeys");
    SaveKeys("MenuKeys", MenuSetting, NUM_MENU_ACTIONS, &sMenuKeys[0][0]);
    PortSettingsSave();
}

// Reads a section's keys into `keys` (count actions); slots not in the
// file stay as they are. Returns a mask: 1 = keys found, 2 = the
// controller's were saved.
static int LoadKeys(const char *section, const char *(*setting)(int), int count, SDL_Scancode *keys)
{
    char key[40];
    const char *version = PortSettingGet(section, "pad");
    int i, k, found = 0, pad = version && strcmp(version, PAD_DEFAULTS_VERSION) == 0;

    for (i = 0; i < count; i++)
    {
        for (k = 0; k < SLOTS; k++)
        {
            const char *name;

            SlotKey(key, sizeof(key), setting(i), k);
            name = PortSettingGet(section, key);
            if (k < PAD_SLOT ? name != NULL : pad)
                keys[i * SLOTS + k] = name ? ScancodeFromName(name) : NO_KEY;
            if (k < PAD_SLOT && name)
                found |= 1;
        }
    }
    return found | (pad ? 2 : 0);
}

static void DefaultMenuKeys(int slots)  // 1 = the keys, 2 = the controller's
{
    int i, k;

    for (i = 0; i < NUM_MENU_ACTIONS; i++)
        for (k = 0; k < SLOTS; k++)
            if (slots & (k < PAD_SLOT ? 1 : 2))
                sMenuKeys[i][k] = sMenuActions[i].keys[k];
}

static void LoadMenuKeys(void)
{
    SDL_Scancode keys[NUM_MENU_ACTIONS][SLOTS];
    int found;

    memset(keys, 0, sizeof(keys));
    found = LoadKeys("MenuKeys", MenuSetting, NUM_MENU_ACTIONS, &keys[0][0]);
    memcpy(sMenuKeys, keys, sizeof(sMenuKeys));
    DefaultMenuKeys((found & 1 ? 0 : 1) | (found & 2 ? 0 : 2));
}

// An old TYPE's keys (0-3, TYPE A the default) and the controller's
// default buttons.
static void PresetKeys(int type, SDL_Scancode keys[NUM_ACTIONS][SLOTS], int slots)
{
    int i;

    for (i = 0; i < NUM_ACTIONS; i++)
    {
        int from = i == LAUNCH ? PINBALL_INPUT_RIGHT_FLIPPER : i;  // LAUNCH: the right flipper's
        if (slots & 1)
            memcpy(&keys[i][0], sPresets[type][from], sizeof(sPresets[type][from]));
        if (slots & 2)
            memcpy(&keys[i][PAD_SLOT], sPadPinball[from], sizeof(sPadPinball[from]));
    }
}

// Pinball keys from a section of the file; FALSE if it has none (the
// controller's buttons are the defaults if it has none of those).
static int LoadPinballKeys(const char *section, SDL_Scancode keys[NUM_ACTIONS][SLOTS])
{
    int found;

    memset(keys, 0, sizeof(SDL_Scancode) * NUM_ACTIONS * SLOTS);
    found = LoadKeys(section, PinballSetting, NUM_ACTIONS, &keys[0][0]);
    if (!(found & 2))
        PresetKeys(0, keys, 2);
    if (!PortSettingGet(section, "launch_set"))
        memcpy(keys[LAUNCH], keys[PINBALL_INPUT_RIGHT_FLIPPER], sizeof(keys[LAUNCH]));
    return (found & 1) || PortSettingGet(section, "set") != NULL;
}

// The pinball keys from the file; TYPE A's until they have been set (and
// PortSetPinballType's from the save, when it is read).
static void LoadConfig(void)
{
    sPinballSaved = LoadPinballKeys("PinballKeys", sPinballKeys);
    if (!sPinballSaved)
        PresetKeys(0, sPinballKeys, 1 | 2);
    sOldEditSaved = LoadPinballKeys("EditKeys", sOldEditKeys);
    LoadMenuKeys();
}

// ---- Pinball keys (called from the game) ----

// The TYPE chosen in the save (the game's BUTTON CONFIG): its keys are
// used until the pinball keys are first set (older setups carry on).
void PortSetPinballType(int type)
{
    if (sPinballSaved)
        return;
    if (type == TYPE_EDIT && sOldEditSaved)
        memcpy(sPinballKeys, sOldEditKeys, sizeof(sPinballKeys));
    else
        PresetKeys(type >= 0 && type < TYPE_EDIT ? type : 0, sPinballKeys, 1 | 2);
}

int PortPinballActionHeld(int action, int gbaMask)
{
    int k;

    for (k = 0; k < SLOTS; k++)
    {
        if (sPinballKeys[action][k] != NO_KEY && sKeys[sPinballKeys[action][k]])
            return TRUE;
    }
    if (PortTouchActionHeld(action) || PortTestBotAction(action))
        return TRUE;
    return gbaMask && (sScripted & gbaMask) == gbaMask;
}

// Short key names in capitals, for the game's small screens.
static const char *KeyName(SDL_Scancode key)
{
    static const struct { SDL_Scancode key; const char *name; } names[] = {
        {SDL_SCANCODE_LSHIFT, "LSHIFT"}, {SDL_SCANCODE_RSHIFT, "RSHIFT"},
        {SDL_SCANCODE_LCTRL, "LCTRL"}, {SDL_SCANCODE_RCTRL, "RCTRL"},
        {SDL_SCANCODE_LALT, "LALT"}, {SDL_SCANCODE_RALT, "RALT"},
        {SDL_SCANCODE_LGUI, "LWIN"}, {SDL_SCANCODE_RGUI, "RWIN"},
        {SDL_SCANCODE_RETURN, "ENTER"},
        {SDL_SCANCODE_ESCAPE, "ESC"}, {SDL_SCANCODE_BACKSLASH, "\\"},
        {SDL_SCANCODE_BACKSPACE, "BKSP"}, {SDL_SCANCODE_CAPSLOCK, "CAPS"},
        {SDL_SCANCODE_COMMA, "COMMA"}, {SDL_SCANCODE_PAGEUP, "PGUP"}, {SDL_SCANCODE_PAGEDOWN, "PGDN"},
        {SDL_SCANCODE_INSERT, "INS"}, {SDL_SCANCODE_DELETE, "DEL"},
        {MOUSE_LEFT, "LCLICK"}, {MOUSE_RIGHT, "RCLICK"},
    };
    static char buf[16];
    const char *sdl;
    size_t i, n = 0, max = 6;

    if (IS_PAD(key))
        return PortPadCodeLabel(key - PORT_PAD_BASE);
    sdl = SDL_GetScancodeName(key);
    for (i = 0; i < ARRAY_COUNT(names); i++)
    {
        if (names[i].key == key)
            return names[i].name;
    }
    if (strncmp(sdl, "Keypad ", 7) == 0)
    {
        // The number pad's keys: NUM 1, NUM ENTER, NUM + ("Keypad" is too long).
        memcpy(buf, "NUM ", 4);
        n = 4;
        max = 10;
        sdl += 7;
    }
    for (; *sdl && n < max; sdl++)
    {
        if (*sdl != ' ')
            buf[n++] = toupper((unsigned char)*sdl);
    }
    buf[n] = '\0';
    return n ? buf : "?";
}

static void KeysText(const SDL_Scancode keys[2], char *out, size_t size)
{
    if (keys[0] == NO_KEY && keys[1] == NO_KEY)
        snprintf(out, size, "-");
    else if (keys[1] == NO_KEY || keys[0] == NO_KEY)
        snprintf(out, size, "%s", KeyName(keys[0] != NO_KEY ? keys[0] : keys[1]));
    else
    {
        char first[24];
        snprintf(first, sizeof(first), "%s", KeyName(keys[0]));
        snprintf(out, size, "%s, %s", first, KeyName(keys[1]));
    }
}

// Options -> BUTTON CONFIG, with JVPPBPORT_GBAVIEW (no PINBALL KEYS box):
// the pinball keys, written where the GBA shows button pictures (options.c
// hides those; its TYPE choice doesn't change the keys). The row being set
// (editRow, or -1) shows what has been pressed so far, blinking.
#define OPTIONS_KEYS_X 158
#define OPTIONS_KEYS_Y 85
#define OPTIONS_ROW_H 12
#define OPTIONS_TEXT_COLOR 0xFF303030

void PortOptionsKeys(int type, int editRow, int visible)
{
    char text[32];
    int i;

    for (i = 0; i < NUM_PINBALL_INPUTS; i++)
    {
        if (i == editRow && sCapture.active)
        {
            if (!visible)
                continue;
            if (sCapture.count == 0)
                snprintf(text, sizeof(text), "PRESS KEY");
            else
                snprintf(text, sizeof(text), "%s, ?", KeyName(sCapture.keys[0]));
        }
        else
        {
            KeysText(sPinballKeys[i], text, sizeof(text));
        }
        PortDrawText(OPTIONS_KEYS_X, OPTIONS_KEYS_Y + i * OPTIONS_ROW_H, text, OPTIONS_TEXT_COLOR);
    }
}

// Setting a pinball action's keys: the first key pressed, then an optional
// second one (pressing the first again, or waiting, keeps just one). Esc
// cancels and keeps the old keys.
void PortKeyCaptureStart(int action)
{
    memset(&sCapture, 0, sizeof(sCapture));
    sCapture.active = TRUE;
    sCapture.action = action;
    sCapture.pad = sPadTab[0];
}

// Keys held now do no menu action until they're let go (the key just set,
// or the Esc that cancelled, would act on the menu at once).
static void IgnoreHeldKeys(void)
{
    int key;

    for (key = 0; key < NUM_CODES; key++)
        sIgnore[key] = sKeys[key];
}

static void FinishCapture(void)
{
    int i, k, j, slot = sCapture.pad ? PAD_SLOT : 0;

    if (sCapture.menu)
    {
        // A key does one menu action: take the new keys from the others.
        for (i = 0; i < NUM_MENU_ACTIONS; i++)
            for (k = 0; k < SLOTS; k++)
                for (j = 0; j < sCapture.count; j++)
                    if (i != sCapture.action && sMenuKeys[i][k] == sCapture.keys[j])
                        sMenuKeys[i][k] = NO_KEY;
        sMenuKeys[sCapture.action][slot] = sCapture.keys[0];
        sMenuKeys[sCapture.action][slot + 1] = sCapture.keys[1];
        SaveMenuKeys();
    }
    else
    {
        sPinballKeys[sCapture.action][slot] = sCapture.keys[0];
        sPinballKeys[sCapture.action][slot + 1] = sCapture.keys[1];
        SaveConfig();
    }
    sCapture.active = FALSE;
    IgnoreHeldKeys();
}

int PortKeyCaptureUpdate(void)
{
    int key, result = PORT_CAPTURE_WAITING;

    if (!sCapture.active)
        return PORT_CAPTURE_DONE;
    // Both shoulder buttons pressed: LB+RB (the GBA's L+R), even if one of
    // them was taken as the first button a moment ago.
    if (sCapture.pad && sKeys[PAD(PORT_PAD_LB_RB)] && !sLastKeys[PAD(PORT_PAD_LB_RB)])
    {
        sCapture.keys[0] = PAD(PORT_PAD_LB_RB);
        sCapture.keys[1] = NO_KEY;
        sCapture.count = 1;
        FinishCapture();
        return PORT_CAPTURE_DONE;
    }
    for (key = 0; key < NUM_CODES && sCapture.active; key++)
    {
        if (sCancelCapture || (sKeys[key] && !sLastKeys[key] && (key == SDL_SCANCODE_ESCAPE || key == PAD_START)))
        {
            sCapture.active = sCancelCapture = FALSE;
            IgnoreHeldKeys();
            return PORT_CAPTURE_CANCELLED;
        }
        if (!sKeys[key] || sLastKeys[key])
            continue;
        if (sCapture.menu && (key == MOUSE_LEFT || key == MOUSE_RIGHT))
            continue;  // the mouse works the menus as it is
        if (IS_PAD(key) != sCapture.pad)
            continue;  // only the shown tab's device (keys, or the controller)
        if (sCapture.count == 0)
        {
            sCapture.keys[sCapture.count++] = key;
            sCapture.timer = SECOND_KEY_FRAMES;
            result = PORT_CAPTURE_GOT_KEY;
        }
        else
        {
            if (key != sCapture.keys[0])
                sCapture.keys[sCapture.count++] = key;
            FinishCapture();
            return PORT_CAPTURE_DONE;
        }
    }
    if (sCapture.count == 1 && result == PORT_CAPTURE_WAITING && --sCapture.timer <= 0)
    {
        FinishCapture();
        return PORT_CAPTURE_DONE;
    }
    return result;
}

// ---- The PINBALL KEYS and MENU KEYS boxes on the Options screen (screenmenu.c) ----

// An action's keys as text; while they're being set, what has been pressed
// so far (blinking).
static void ActionKeyText(int menu, int action, const SDL_Scancode keys[2], char *out, size_t size)
{
    if (sCapture.active && sCapture.menu == menu && sCapture.action == action)
    {
        if ((PortCurrentFrame() / 16) & 1)
            snprintf(out, size, "%s", "");
        else if (sCapture.count == 0)
            snprintf(out, size, sPadTab[menu] ? "PRESS BUTTON" : "PRESS KEY");
        else
            snprintf(out, size, "%s, ?", KeyName(sCapture.keys[0]));
        return;
    }
    KeysText(keys + (sPadTab[menu] ? PAD_SLOT : 0), out, size);
}

// The PAD tab is shown in the MENU box (menu) or the PINBALL box.
int PortPadTab(int menu)
{
    return sPadTab[menu != 0];
}

void PortSetPadTab(int menu, int pad)
{
    if (!sCapture.active)
        sPadTab[menu != 0] = pad != 0;
}

int PortPinballKeyCount(void)
{
    return NUM_ACTIONS;
}

const char *PortPinballKeyLabel(int action)
{
    return sActionLabels[action];
}

void PortPinballKeyText(int action, char *out, size_t size)
{
    ActionKeyText(FALSE, action, sPinballKeys[action], out, size);
}

// DEFAULTS: the keys or the controller's buttons, whichever are shown.
void PortPinballKeysDefault(void)
{
    PresetKeys(0, sPinballKeys, sPadTab[0] ? 2 : 1);
    SaveConfig();
}

int PortMenuKeyCount(void)
{
    return PortPcMenus() ? NUM_MENU_ACTIONS : MENU_QUIT;  // SAVE AND QUIT last
}

const char *PortMenuKeyLabel(int action)
{
    return sMenuActions[action].label;
}

void PortMenuKeyText(int action, char *out, size_t size)
{
    ActionKeyText(TRUE, action, sMenuKeys[action], out, size);
}

void PortMenuKeyCaptureStart(int action)
{
    PortKeyCaptureStart(action);
    sCapture.menu = TRUE;
    sCapture.pad = sPadTab[1];
}

// A pinball or menu key is being set.
int PortKeyCapturing(void)
{
    return sCapture.active;
}

void PortMenuKeysDefault(void)
{
    DefaultMenuKeys(sPadTab[1] ? 2 : 1);
    SaveMenuKeys();
}

// A key or button is one of the pinball actions'.
static int PinballKey(SDL_Scancode key)
{
    int i;

    for (i = 0; i < NUM_ACTIONS * SLOTS; i++)
    {
        if (sPinballKeys[i / SLOTS][i % SLOTS] == key)
            return TRUE;
    }
    return FALSE;
}

// A menu action's key is held (one that was held when a key was set
// doesn't count until it's let go). While playing, keys that are also
// pinball keys don't count (the pad's B is the right flipper and BACK).
static int MenuHeld(int action, int playing)
{
    int k;

    for (k = 0; k < SLOTS; k++)
    {
        SDL_Scancode key = sMenuKeys[action][k];
        if (key != NO_KEY && sKeys[key] && !sIgnore[key] && !(playing && PinballKey(key)))
            return TRUE;
    }
    return FALSE;
}

// A game is being played: no pause, banner or other break, and no
// menu panel (Mart, Evolution Select) is open.
int PortPlaying(void)
{
    return gMain.mainState == STATE_GAME_MAIN && gMain.modeChangeFlags == 0 && !gMain.shopPanelActive;
}

// What BACK presses: B in menus, the Mart and the game's yes/no prompts;
// Start (pause, and resume) in a game. Chosen when it is pressed, so
// holding it doesn't pause and then resume.
static u16 BackButton(void)
{
    if (gMain.mainState != STATE_GAME_MAIN || PortGameChoiceOpen())
        return B_BUTTON;
    if (gMain.modeChangeFlags == 0 || (gMain.modeChangeFlags & MODE_CHANGE_PAUSE))
        return START_BUTTON;
    return B_BUTTON;
}

// ---- Test scripts ----

// Calls `found(name, length)` for each entry of a script list whose frame
// range covers `frame` (entries last `hold` frames).
static void ScriptEntries(const char *p, u32 frame, u32 hold, void (*found)(const char *, size_t))
{
    while (p && *p)
    {
        char *colon;
        u32 at = strtoul(p, &colon, 10);
        const char *end = strchr(p, ',');
        size_t len = end ? (size_t)(end - colon - 1) : strlen(colon + 1);

        if (*colon == ':' && frame >= at && frame < at + hold)
            found(colon + 1, len);
        p = end ? end + 1 : NULL;
    }
}

static void ScriptedPress(const char *name, size_t len)
{
    static const struct { const char *name; u16 button; } names[] = {
        {"A", A_BUTTON}, {"B", B_BUTTON}, {"SELECT", SELECT_BUTTON}, {"START", START_BUTTON},
        {"RIGHT", DPAD_RIGHT}, {"LEFT", DPAD_LEFT}, {"UP", DPAD_UP}, {"DOWN", DPAD_DOWN},
        {"R", R_BUTTON}, {"L", L_BUTTON},
    };
    size_t i;

    if (len > 4 && strncmp(name, "PAD_", 4) == 0)
    {
        char button[32];
        snprintf(button, sizeof(button), "%.*s", (int)(len - 4), name + 4);
        if (PortPadCodeFromName(button) >= 0)
            sKeys[PAD(PortPadCodeFromName(button))] = TRUE;
        else
            fprintf(stderr, "JVPPBPort: unknown controller button in test script: %s\n", button);
        return;
    }
    if (len > 4 && strncmp(name, "KEY_", 4) == 0)
    {
        char key[64];
        snprintf(key, sizeof(key), "%.*s", (int)(len - 4), name + 4);
        for (i = 0; key[i]; i++)
        {
            if (key[i] == '_')
                key[i] = ' ';
        }
        if (ScancodeFromName(key) != NO_KEY)
            sKeys[ScancodeFromName(key)] = TRUE;
        else
            fprintf(stderr, "JVPPBPort: unknown key name in test script: %s\n", key);
        return;
    }
    for (i = 0; i < ARRAY_COUNT(names); i++)
    {
        if (strlen(names[i].name) == len && strncmp(name, names[i].name, len) == 0)
            sScripted |= names[i].button;
    }
}

static void ScriptedMouse(const char *entry, size_t len)
{
    char buf[64];
    int x, y, n;

    snprintf(buf, sizeof(buf), "%.*s", (int)len, entry);
    if (sscanf(buf, "move:%d:%d", &x, &y) == 2)
    {
        sMouse.x = x;
        sMouse.y = y;
        sMouse.inside = TRUE;
        sMouseMoved = TRUE;
    }
    else if (strcmp(buf, "click") == 0)
        sMouseClicks++;
    else if (strcmp(buf, "down") == 0)  // left button held from here...
        sMouseClicks++, sMouseDown[0] = TRUE;
    else if (strcmp(buf, "up") == 0)    // ...to here (a drag)
        sMouseDown[0] = FALSE;
    else if (strcmp(buf, "rclick") == 0)
        sMouseRightClicks++;
    else if (sscanf(buf, "wheel:%d", &n) == 1)
        sMouseWheel += n;
    else if (sscanf(buf, "hwheel:%d", &n) == 1)
        sMouseHWheel += n;
}

static void RunScripts(u32 frame)
{
    if (sHoldEvery)
    {
        char *colon;
        u32 every = strtoul(sHoldEvery, &colon, 10);
        if (every && *colon == ':' && frame % every < 6)
            ScriptedPress(colon + 1, strlen(colon + 1));
    }
    ScriptEntries(sPresses, frame, 6, ScriptedPress);
    ScriptEntries(sMouseScript, frame, 1, ScriptedMouse);
}

// ---- Each frame ----

void PortInputInit(void)
{
    sPresses = getenv("JVPPBPORT_PRESS");
    sHoldEvery = getenv("JVPPBPORT_HOLD_EVERY");
    sMouseScript = getenv("JVPPBPORT_MOUSE");
    sMouse.x = sMouse.y = -1;
    LoadConfig();
    PortPadInit();
}

void PortInputEvent(const SDL_Event *event)
{
    PortPadEvent(event);
    switch (event->type)
    {
    case SDL_KEYDOWN:
        if (event->key.keysym.scancode == SDL_SCANCODE_AC_BACK)
            sBackPressed = TRUE;
        break;
    case SDL_MOUSEMOTION:
        sMouse.x = event->motion.x;
        sMouse.y = event->motion.y;
        sMouse.inside = TRUE;
        sMouseMoved = TRUE;
        break;
    case SDL_MOUSEBUTTONDOWN:
        sMouse.x = event->button.x;
        sMouse.y = event->button.y;
        sMouse.inside = TRUE;
        if (event->button.button == SDL_BUTTON_LEFT)
            sMouseClicks++;
        else if (event->button.button == SDL_BUTTON_RIGHT)
            sMouseRightClicks++;
        if (event->button.button == SDL_BUTTON_LEFT || event->button.button == SDL_BUTTON_RIGHT)
        {
            sMouseDown[event->button.button == SDL_BUTTON_RIGHT] = TRUE;
            sMousePressed[event->button.button == SDL_BUTTON_RIGHT] = TRUE;
        }
        break;
    case SDL_MOUSEBUTTONUP:
        if (event->button.button == SDL_BUTTON_LEFT || event->button.button == SDL_BUTTON_RIGHT)
            sMouseDown[event->button.button == SDL_BUTTON_RIGHT] = FALSE;
        break;
    case SDL_MOUSEWHEEL:
        sMouseWheel += event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event->wheel.y : event->wheel.y;
        sMouseHWheel += event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event->wheel.x : event->wheel.x;
        break;
    case SDL_WINDOWEVENT:
        if (event->window.event == SDL_WINDOWEVENT_LEAVE)
            sMouse.inside = FALSE;
        break;
    }
}

// Esc for one frame (pause while playing, back in menus), as if Android's
// Back were pressed.
void PortInputPressBack(void)
{
    sBackPressed = TRUE;
}

// SAVE AND QUIT: a game in progress is saved first (as the pause menu's
// SAVE does, then the program closes). If it can't be saved just now (a banner, the ball being
// launched...), the next moment it can is waited for, at most 5 seconds.
static void SaveAndQuitKey(int menuPlaying)
{
    int held = PortPcMenus() && MenuHeld(MENU_QUIT, menuPlaying);

    if (held && !sLastQuit)
        sQuitWait = 1;
    sLastQuit = held;
    if (!sQuitWait)
        return;
    if (gMain.mainState != STATE_GAME_MAIN || gMain.gameExitState != 0
        || (gMain.modeChangeFlags & MODE_CHANGE_END_OF_GAME) || PortSaveGameInProgress() || ++sQuitWait > 300)
        PortQuit();
}

// The GBA buttons held this frame (menu keys, mouse, test script).
u16 PortReadInput(u32 frame)
{
    const Uint8 *keyboard = SDL_GetKeyboardState(NULL);
    u16 pressed = 0;
    int pinballKeyNew = FALSE, playing, menuPlaying, back, deviceBack = FALSE;
    size_t i;

    memcpy(sLastKeys, sKeys, sizeof(sKeys));
    memcpy(sKeys, keyboard, SDL_NUM_SCANCODES);
    PortPadRead(sKeys + PORT_PAD_BASE);
    // Alt+Enter and F11 switch fullscreen (platform.c); they aren't game keys.
    if (sKeys[SDL_SCANCODE_LALT] || sKeys[SDL_SCANCODE_RALT])
        sKeys[SDL_SCANCODE_RETURN] = sKeys[SDL_SCANCODE_KP_ENTER] = 0;
    sKeys[SDL_SCANCODE_F11] = 0;
    if (PortTouchFrame())
        sBackPressed = TRUE;  // the touch screen's pause button
    if (sBackPressed && !sLastBack)
    {
        deviceBack = TRUE;
        sBackPressed = FALSE;
    }
    sKeys[MOUSE_LEFT] = sMouseDown[0] || sMousePressed[0];
    sKeys[MOUSE_RIGHT] = sMouseDown[1] || sMousePressed[1];
    sMousePressed[0] = sMousePressed[1] = FALSE;
    sScripted = 0;
    RunScripts(frame);
    if (sKeys[PAD(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)] && sKeys[PAD(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)])
        sKeys[PAD(PORT_PAD_LB_RB)] = TRUE;  // also for scripted shoulder buttons
    for (i = 0; i < NUM_CODES; i++)
        sIgnore[i] &= sKeys[i];

    if (sCapture.active)
    {
        // Setting a key: the keys go there, not to the menu. Android's Back
        // cancels it.
        sCancelCapture = deviceBack;
        sMouseMoved = sMouseClicks = sMouseRightClicks = sMouseWheel = sMouseHWheel = 0;
        return 0;
    }
    // While playing, keys that are also pinball keys aren't menu keys,
    // except at the game's yes/no prompts (B says no there).
    playing = PortPlaying();
    menuPlaying = playing && !PortGameChoiceOpen();
    for (i = 0; i < NUM_MENU_ACTIONS; i++)
    {
        if (MenuHeld(i, menuPlaying))
            pressed |= sMenuActions[i].button;
    }
    back = MenuHeld(MENU_BACK, menuPlaying) || deviceBack;
    if (back && !sLastBack)
        sBackButton = BackButton();
    if (back)
        pressed |= sBackButton;
    sLastBack = back;
    SaveAndQuitKey(menuPlaying);
    pressed |= sScripted;

    // The attract-mode demo ends on any button; pinball keys count too.
    for (i = 0; i < NUM_ACTIONS * SLOTS; i++)
    {
        SDL_Scancode key = sPinballKeys[i / SLOTS][i % SLOTS];
        if (key != NO_KEY && sKeys[key] && !sLastKeys[key])
            pinballKeyNew = TRUE;
    }
    for (i = 0; i < NUM_PINBALL_INPUTS; i++)
    {
        if (PortTouchActionHeld(i))
            pinballKeyNew = TRUE;
    }
    if (pinballKeyNew && gMain.mainState == STATE_GAME_IDLE)
        pressed |= A_BUTTON;

    sMouse.moved = sMouseMoved;
    sMouse.down = sMouseDown[0];
    // While playing, a mouse button bound to a pinball action only works
    // that action (and isn't a menu click).
    // At the game's yes/no prompts it still clicks the prompt's picture.
    sMouse.clicks = playing && MouseBound(MOUSE_LEFT) && !PortGameChoiceOpen() ? 0 : sMouseClicks;
    sMouse.clicksOnTargets = playing && MouseBound(MOUSE_LEFT);
    sMouse.rightClicks = playing && MouseBound(MOUSE_RIGHT) ? 0 : sMouseRightClicks;
    sMouse.wheel = sMouseWheel;
    sMouse.hwheel = sMouseHWheel;
    sMouse.keyPressed = memcmp(sKeys, sLastKeys, NUM_REAL_KEYS) != 0
                     || memcmp(sKeys + PORT_PAD_BASE, sLastKeys + PORT_PAD_BASE, PORT_PAD_CODES) != 0;
    sMouse.onScreen = sMouse.inside && PortCanvasToScreen(sMouse.x, sMouse.y, &sMouse.sx, &sMouse.sy);
    sMouseMoved = sMouseClicks = sMouseRightClicks = sMouseWheel = sMouseHWheel = 0;
    pressed |= PortMouseButtons(&sMouse);  // the mouse, or fingers (touch.c), in menus
    return pressed;
}
