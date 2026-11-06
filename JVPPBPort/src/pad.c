// JVPPBPort: game controllers, on PC and
// Android, through SDL's game controller support (any pad SDL knows,
// buttons named by their place on an Xbox pad).
//
// - Every controller's buttons, sticks and triggers count as extra "keys"
//   for input.c (PORT_PAD_BASE + a pad code), so they can be bound to menu
//   and pinball actions next to the keyboard's keys (each action has two
//   keyboard and two controller slots; input.c). Sticks and triggers are
//   held past PRESS_AT and let go below RELEASE_AT. All connected pads
//   work at once.
// - Rumble (Options -> RUMBLE, the game's Game Boy Player rumble): on
//   Android, the phone vibrates while the on-screen controls are on (touch.c);
//   with them off, the controller rumbles if it can (SDL, or Android's own
//   controller vibration through the Java side), else the phone. On PC the
//   controllers rumble.
//
// - Windows: SDL's DirectInput search is off by default. Listing the
//   computer's input devices took it 2-4 s at start on some PCs (a
//   frozen window); Xbox, PlayStation and Switch pads don't need it. Old
//   DirectInput-only pads need `DirectInput=1` under [Controls] in
//   jvppbport.ini (a hidden setting, see README.md).
//
// Test mode: JVPPBPORT_RUMBLE_TRACE=1 prints where the rumble goes.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "jvppbport.h"

#define TRUE 1
#define FALSE 0

#define MAX_PADS 8
#define PRESS_AT 16000    // a stick or trigger counts as held past this (of 32767)
#define RELEASE_AT 11000  // and is let go below this
#define RUMBLE_MS 2000    // a rumble lasts this long unless turned off (renewed while on)
#define RUMBLE_RENEW 60   // frames

// Pad codes past SDL's buttons: stick directions and triggers (jvppbport.h).
#define PAD_LX_NEG PORT_PAD_LX_NEG
#define PAD_LT PORT_PAD_LT
#define PAD_RT PORT_PAD_RT
#define PAD_LB_RB PORT_PAD_LB_RB
#define PAD_CODE_COUNT PORT_PAD_CODE_COUNT
SDL_COMPILE_TIME_ASSERT(pad_codes, PORT_PAD_LX_NEG == SDL_CONTROLLER_BUTTON_MAX && PORT_PAD_CODE_COUNT <= PORT_PAD_CODES);
SDL_COMPILE_TIME_ASSERT(pad_base, PORT_PAD_BASE == SDL_NUM_SCANCODES);

static SDL_GameController *sPads[MAX_PADS];
static SDL_GameController *sLastPad;  // the one last pressed (its button names are shown)
static int sReady;

void PortPadInit(void)
{
    // Not in the hidden copies that draw the table pictures: on Android
    // they are forked from the app and can't reach its Java side, which
    // SDL's controller code uses.
    if (gPortForkedCopy || getenv("JVPPBPORT_TABLEDUMP"))
        return;
    // Face buttons by place (SDL's A = the bottom one, B = the right one),
    // also on Nintendo pads, whose letters are swapped (see PortPadCodeLabel).
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
    SDL_SetHint(SDL_HINT_DIRECTINPUT_ENABLED, PortSettingGetInt("Controls", "DirectInput", 0) ? "1" : "0");
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
        fprintf(stderr, "JVPPBPort: no game controller support: %s\n", SDL_GetError());
    else
        sReady = TRUE;
}

static void Open(int index)
{
    SDL_GameController *pad;
    int i;

    if (!SDL_IsGameController(index) || !(pad = SDL_GameControllerOpen(index)))
        return;
    for (i = 0; i < MAX_PADS; i++)
    {
        if (sPads[i] == pad)
            return;  // already open (SDL counts each open)
    }
    for (i = 0; i < MAX_PADS; i++)
    {
        if (!sPads[i])
        {
            sPads[i] = pad;
            fprintf(stderr, "JVPPBPort: controller connected: %s\n", SDL_GameControllerName(pad));
            return;
        }
    }
    SDL_GameControllerClose(pad);
}

void PortPadEvent(const SDL_Event *event)
{
    int i;

    if (event->type == SDL_CONTROLLERDEVICEADDED)
        Open(event->cdevice.which);
    else if (event->type == SDL_CONTROLLERDEVICEREMOVED)
    {
        for (i = 0; i < MAX_PADS; i++)
        {
            if (sPads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(sPads[i])) == event->cdevice.which)
            {
                fprintf(stderr, "JVPPBPort: controller disconnected\n");
                if (sLastPad == sPads[i])
                    sLastPad = NULL;
                SDL_GameControllerClose(sPads[i]);
                sPads[i] = NULL;
            }
        }
    }
}

int PortPadCount(void)
{
    int i, n = 0;

    for (i = 0; i < MAX_PADS; i++)
        n += sPads[i] != NULL;
    return n;
}

static int AxisHeld(int value, int wasHeld)
{
    return value > (wasHeld ? RELEASE_AT : PRESS_AT);
}

// Fills held[PORT_PAD_CODES] with what any controller holds now (`held`
// holds last frame's on entry, for the sticks' let-go point). Returns 1 if
// something was newly pressed.
int PortPadRead(uint8_t *held)
{
    static const SDL_GameControllerAxis sticks[4] = {
        SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY, SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY,
    };
    uint8_t now[PORT_PAD_CODES];
    int i, code, pressed = FALSE;

    memset(now, 0, sizeof(now));
    for (i = 0; i < MAX_PADS; i++)
    {
        SDL_GameController *pad = sPads[i];
        int newHere = FALSE;

        if (!pad)
            continue;
        for (code = 0; code < SDL_CONTROLLER_BUTTON_MAX; code++)
        {
            if (SDL_GameControllerGetButton(pad, code))
            {
                now[code] = TRUE;
                newHere |= !held[code];
            }
        }
        for (code = 0; code < 4; code++)
        {
            int v = SDL_GameControllerGetAxis(pad, sticks[code]);
            int neg = PAD_LX_NEG + code * 2, pos = neg + 1;

            if (AxisHeld(-v, held[neg]))
                now[neg] = TRUE, newHere |= !held[neg];
            if (AxisHeld(v, held[pos]))
                now[pos] = TRUE, newHere |= !held[pos];
        }
        if (AxisHeld(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT), held[PAD_LT]))
            now[PAD_LT] = TRUE, newHere |= !held[PAD_LT];
        if (AxisHeld(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT), held[PAD_RT]))
            now[PAD_RT] = TRUE, newHere |= !held[PAD_RT];
        if (newHere)
            sLastPad = pad;
        pressed |= newHere;
    }
    now[PAD_LB_RB] = now[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] && now[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER];
    memcpy(held, now, sizeof(now));
    return pressed;
}

// ---- Names ----

// In the settings file: SDL's names ("a", "dpup", "leftshoulder"), sticks
// as "leftx-" etc., triggers "lefttrigger".
static const char *const sAxisNames[] = {
    "leftx-", "leftx+", "lefty-", "lefty+", "rightx-", "rightx+", "righty-", "righty+", "lefttrigger", "righttrigger",
    "leftshoulder+rightshoulder",
};

const char *PortPadCodeName(int code)
{
    if (code >= 0 && code < SDL_CONTROLLER_BUTTON_MAX)
        return SDL_GameControllerGetStringForButton(code);
    if (code >= PAD_LX_NEG && code < PAD_CODE_COUNT)
        return sAxisNames[code - PAD_LX_NEG];
    return NULL;
}

int PortPadCodeFromName(const char *name)
{
    int code;

    for (code = 0; code < PAD_CODE_COUNT; code++)
    {
        if (PortPadCodeName(code) && SDL_strcasecmp(PortPadCodeName(code), name) == 0)
            return code;
    }
    return -1;
}

// Short names in capitals for the Options screen, as printed on the last
// used controller (Xbox style unless it's a PlayStation or Nintendo pad).
// The face buttons go by place: SDL's A is the bottom one, B the right one.
const char *PortPadCodeLabel(int code)
{
    static const char *const xbox[PAD_CODE_COUNT] = {
        "A", "B", "X", "Y", "SELECT", "HOME", "START", "L3", "R3", "LB", "RB",
        "D-UP", "D-DOWN", "D-LEFT", "D-RIGHT", "MISC", "P1", "P2", "P3", "P4", "TPAD",
        "LS LEFT", "LS RIGHT", "LS UP", "LS DOWN", "RS LEFT", "RS RIGHT", "RS UP", "RS DOWN", "LT", "RT", "LB+RB",
    };
    SDL_GameControllerType type = sLastPad ? SDL_GameControllerGetType(sLastPad)
                                : PortPadCount() ? SDL_GameControllerGetType(sPads[0]) : SDL_CONTROLLER_TYPE_UNKNOWN;
    int ps = type == SDL_CONTROLLER_TYPE_PS3 || type == SDL_CONTROLLER_TYPE_PS4 || type == SDL_CONTROLLER_TYPE_PS5;
    int nintendo = type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO;

    if (code < 0 || code >= PAD_CODE_COUNT)
        return "?";
    if (ps)
    {
        static const char *const names[] = {"CROSS", "CIRCLE", "SQUARE", "TRIANGLE"};
        if (code <= SDL_CONTROLLER_BUTTON_Y)
            return names[code];
        if (code == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
            return "L1";
        if (code == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
            return "R1";
        if (code == PAD_LT)
            return "L2";
        if (code == PAD_RT)
            return "R2";
        if (code == PAD_LB_RB)
            return "L1+R1";
    }
    if (nintendo)
    {
        static const char *const names[] = {"B", "A", "Y", "X"};  // by place: bottom, right, left, top
        if (code <= SDL_CONTROLLER_BUTTON_Y)
            return names[code];
        if (code == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
            return "L";
        if (code == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
            return "R";
        if (code == PAD_LT)
            return "ZL";
        if (code == PAD_RT)
            return "ZR";
        if (code == PAD_LB_RB)
            return "L+R";
        if (code == SDL_CONTROLLER_BUTTON_BACK)
            return "-";
        if (code == SDL_CONTROLLER_BUTTON_START)
            return "+";
    }
    return xbox[code];
}

// ---- Rumble ----

// Every controller SDL can rumble; returns 1 if at least one could.
static int PadRumble(int on)
{
    int i, done = FALSE;

    for (i = 0; i < MAX_PADS; i++)
    {
        if (sPads[i] && SDL_GameControllerRumble(sPads[i], on ? 0xC000 : 0, on ? 0xC000 : 0, on ? RUMBLE_MS : 0) == 0)
            done = TRUE;
    }
    return done;
}

static void Stop(int where)
{
    if (where == 1)
        PortAndroidVibrate(0);
    else if (where == 2)
        PadRumble(0);
    else if (where == 3)
        PortAndroidPadVibrate(0);
}

// The game's rumble motor, on or off; called every frame of play (gbplayer.c).
// While on, it is renewed now and then (each start lasts RUMBLE_MS), and
// moves if the place to rumble changes (a pad connected, touch controls
// switched).
void PortRumble(int on)
{
    static int now, where, renew;
    int to;  // 0 nowhere, 1 phone, 2 controller (SDL), 3 controller (Android)

    if (gPortForkedCopy)
        return;
    on = on != 0;
    if (on == now && (!on || ++renew < RUMBLE_RENEW))
        return;
    renew = 0;
    now = on;
    if (!on)
    {
        Stop(where);
        where = 0;
        return;
    }
#ifdef __ANDROID__
    if (PortTouchControls())
        to = 1;
    else if (PortPadCount() && PadRumble(1))
        to = 2;
    else if (PortPadCount() && PortAndroidPadVibrate(1))
        to = 3;
    else
        to = 1;
    if (to == 1)
        PortAndroidVibrate(1);
#else
    to = PadRumble(1) ? 2 : 0;
#endif
    if (to != where)
    {
        Stop(where);
        if (getenv("JVPPBPORT_RUMBLE_TRACE"))
            fprintf(stderr, "rumble: %s on frame %u\n", to == 1 ? "phone" : to ? "controller" : "nowhere", PortCurrentFrame());
    }
    where = to;
}
