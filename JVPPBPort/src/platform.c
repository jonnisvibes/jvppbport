// JVPPBPort: the PC side. Opens the window, passes keyboard and mouse to
// input.c, and runs the game's frame loop at the GBA's speed.
//
// The game's own loop (AgbMain in src/main.c) never returns. Once per frame it
// calls VBlankIntrWait(), which on the GBA sleeps until the screen finishes
// drawing. Here that call draws the frame scanline by scanline, shows it,
// reads input, waits out the rest of the frame time, and then runs the game's
// VBlank interrupt handler, just like the hardware would.
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include "global.h"
#include "main.h"
#include "constants/fields.h"
#include "jvppbport.h"
#include "version.h"

#define GBA_FPS 59.7275

#define IO16(off) (*(u16 *)&gPortIo[off])

static SDL_Window *sWindow;
static SDL_Renderer *sRenderer;
static SDL_Texture *sTexture;
static int sTexW, sTexH;  // its size
static uint32_t sFrame[PORT_MENU_MAX_H * PORT_MENU_MAX_W];  // the canvas (see PortCanvasWidth)
static Uint64 sNextFrameTime;
static jmp_buf sResetJump;
static u32 sFrameCount;

// Test mode, for checking the port without a person at the keyboard:
//   JVPPBPORT_SHOTS=60,300,900   save shot_<frame>.bmp in the data folder (system.c) at those frames
//   JVPPBPORT_EXIT=900           quit after that frame
//   JVPPBPORT_SNAPSHOT=4650      take an F12 bug snapshot at those frames
//   JVPPBPORT_PRESS, JVPPBPORT_HOLD_EVERY, JVPPBPORT_MOUSE: buttons, keys and
//                                     mouse at given frames, see input.c
//   JVPPBPORT_TEST_FIELD=3            the next game started plays this field instead
//                                     (0 Ruby, 1 Sapphire, 2 Dusclops, 3 Kecleon,
//                                     4 Kyogre, 5 Groudon, 6 Rayquaza, 7 Spheal)
//   JVPPBPORT_SAVE=path               use this save file instead of the real one
//   JVPPBPORT_TEST_EVO=4200            force evolution mode during a game (see testhooks.c)
//   JVPPBPORT_FAST=1                  don't wait between frames (runs as fast as it can)
// Add SDL_VIDEODRIVER=offscreen to run without opening a window.
static const char *sShots;
static u32 sExitFrame;
static int sTestField = -1;
static bool8 sFast;

// The window's size and place are kept in jvppbport.ini ([Window], see
// settings.c) when the game closes or switches fullscreen, and restored next
// time if that place is still on a screen (else a default window is used).
// Normal size and place are remembered while maximized or fullscreen, so
// leaving those goes back to them.
static SDL_Rect sNormalRect;  // the window when neither maximized nor fullscreen

// A forked copy drawing a table picture (Android, see gamedata.c) has no
// window, sound or input at all: it only runs the game.
static int sHeadless;

static int IsFullscreen(void)
{
    return (SDL_GetWindowFlags(sWindow) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
}

// Options -> SCREEN -> SCALE ([Display] Scale in jvppbport.ini: Fill, Snap
// or a number):
// - FILL (0): the picture as big as the window allows, keeping its shape
//   (the window can be any size).
// - SNAP FILL (PORT_SCALE_SNAP): the same, but only whole-number sizes (the
//   biggest that fits, centered on black), so every pixel (and LCD filter
//   cell) is the same size. Below 1x it shrinks as FILL does.
// - N: exactly N times (the window is made that size; in fullscreen or a
//   bigger window the picture stays N times, centered on black; a window too
//   small for it gets the biggest whole number that fits).
// On touch screens the same choices; there a whole-number scale changes how
// many picture pixels the screen has (touch.c lays it out, so it fills the
// screen at that scale).
#define MAX_SCALE 16
#define SCALE_UNREAD (-100)
static int sScale = SCALE_UNREAD;
static int sPicW = PORT_CANVAS_WIDTH, sPicH = PORT_CANVAS_HEIGHT;  // the picture last shown

int PortScaleMode(void)
{
    if (sScale == SCALE_UNREAD)
    {
        const char *s = PortSettingGet("Display", "Scale");

        sScale = s && SDL_strcasecmp(s, "Snap") == 0 ? PORT_SCALE_SNAP : s ? atoi(s) : 0;
        if (sScale < PORT_SCALE_SNAP || sScale > MAX_SCALE)
            sScale = 0;
    }
    return sScale;
}

// The smallest whole-number scale offered.
int PortScaleMin(void)
{
    int n = 1, max;

    if (PortTouchEnabled())
        PortTouchScaleRange(&n, &max);
    return n;
}

// The biggest scale offered: the most that fits the window's screen
// (in fullscreen).
int PortScaleMax(void)
{
    SDL_Rect bounds;
    int display = sWindow ? SDL_GetWindowDisplayIndex(sWindow) : 0, n;

    if (PortTouchEnabled())
    {
        int min;

        PortTouchScaleRange(&min, &n);
        return n;
    }
    if (SDL_GetDisplayBounds(display < 0 ? 0 : display, &bounds) != 0)
        return 1;
    n = bounds.w / sPicW < bounds.h / sPicH ? bounds.w / sPicW : bounds.h / sPicH;
    return n < 1 ? 1 : n > MAX_SCALE ? MAX_SCALE : n;
}

// Makes a normal window w x h, keeping its middle where it was (moved back
// onto its screen if it would stick out).
static void ResizeWindowAroundMiddle(int w, int h)
{
    SDL_Rect usable;
    int x, y, oldW, oldH, display;

    if (SDL_GetWindowFlags(sWindow) & SDL_WINDOW_MAXIMIZED)
        SDL_RestoreWindow(sWindow);
    SDL_GetWindowPosition(sWindow, &x, &y);
    SDL_GetWindowSize(sWindow, &oldW, &oldH);
    x += oldW / 2 - w / 2;
    y += oldH / 2 - h / 2;
    display = SDL_GetWindowDisplayIndex(sWindow);
    if (SDL_GetDisplayUsableBounds(display < 0 ? 0 : display, &usable) == 0)
    {
        if (x + w > usable.x + usable.w)
            x = usable.x + usable.w - w;
        if (y + h > usable.y + usable.h)
            y = usable.y + usable.h - h;
        if (x < usable.x)
            x = usable.x;
        if (y < usable.y)
            y = usable.y;
    }
    SDL_SetWindowSize(sWindow, w, h);
    SDL_SetWindowPosition(sWindow, x, y);
}

// Makes a normal window the size of the chosen scale.
static void SizeWindowForScale(void)
{
    int n = PortScaleMode();

    if (sWindow && n > 0 && !IsFullscreen() && !PortTouchEnabled())
        ResizeWindowAroundMiddle(sPicW * n, sPicH * n);
}

void PortSetScaleMode(int n)
{
    if (n < PORT_SCALE_SNAP || n > MAX_SCALE || n == PortScaleMode())
        return;
    sScale = n;
    if (n == PORT_SCALE_SNAP)
        PortSettingSet("Display", "Scale", "Snap");
    else if (n == 0)
        PortSettingSet("Display", "Scale", "Fill");
    else
        PortSettingSetInt("Display", "Scale", n);
    PortSettingsSave();
    if (PortTouchEnabled())
        PortTouchRelayout();
    else
        SizeWindowForScale();
}

// Options -> SCREEN -> TATE MODE ([Display] Tate in jvppbport.ini): the
// picture turned a quarter, for a monitor stood on its side while Windows
// stays in landscape. Everything after the game's canvas sees a sideways
// picture (the window's shape, the scale, the LCD masks), so ON suits a
// monitor turned clockwise (most stands; Windows calls that Portrait) and
// REVERSE one turned the other way. Keys are unchanged; the mouse is mapped back
// (PortTateToCanvas). PC only.
static const char *const sTateNames[PORT_TATE_COUNT] = {"Off", "On", "Reverse"};
static int sTate = -1;

int PortTate(void)
{
    if (PortTouchEnabled() || !gPortStaticView)
        return PORT_TATE_OFF;
    if (sTate < 0)
    {
        const char *s = PortSettingGet("Display", "Tate");
        int i;

        sTate = PORT_TATE_OFF;
        for (i = 0; s && i < PORT_TATE_COUNT; i++)
            if (SDL_strcasecmp(s, sTateNames[i]) == 0)
                sTate = i;
    }
    return sTate;
}

void PortSetTate(int mode)
{
    int was = PortTate();

    if (PortTouchEnabled() || !gPortStaticView || mode < 0 || mode >= PORT_TATE_COUNT || mode == was)
        return;
    sTate = mode;
    PortSettingSet("Display", "Tate", sTateNames[mode]);
    PortSettingsSave();
    if ((was != PORT_TATE_OFF) == (mode != PORT_TATE_OFF))
        return;  // already sideways: the same window
    {
        int t = sPicW;

        sPicW = sPicH;
        sPicH = t;
    }
    if (!sWindow || IsFullscreen() || (SDL_GetWindowFlags(sWindow) & SDL_WINDOW_MAXIMIZED))
        return;  // those stay as they are; the picture just fits them
    if (PortScaleMode() > 0)
        SizeWindowForScale();
    else
    {
        // The window turned too, made smaller if that doesn't fit the screen.
        SDL_Rect usable;
        int w, h, display = SDL_GetWindowDisplayIndex(sWindow), t;

        SDL_GetWindowSize(sWindow, &w, &h);
        t = w, w = h, h = t;
        if (SDL_GetDisplayUsableBounds(display < 0 ? 0 : display, &usable) == 0)
        {
            if (w > usable.w)
                h = h * usable.w / w, w = usable.w;
            if (h > usable.h)
                w = w * usable.h / h, h = usable.h;
        }
        ResizeWindowAroundMiddle(w, h);
    }
}

// A point of the sideways picture, as the canvas's pixels.
void PortTateToCanvas(int *x, int *y)
{
    int px = *x, py = *y;

    if (PortTate() == PORT_TATE_ON)
        *x = sPicH - 1 - py, *y = px;
    else if (PortTate() == PORT_TATE_REVERSE)
        *x = py, *y = sPicW - 1 - px;
}

// Where the picture (w x h) goes in a window of outW x outH pixels: its
// top-left corner and its scale.
static void PictureRect(int outW, int outH, int w, int h, float *x, float *y, float *scale)
{
    float fit = (float)outW / w < (float)outH / h ? (float)outW / w : (float)outH / h;
    int n = PortScaleMode();

    if (PortTouchEnabled())
    {
        // touch.c made the picture the size for its scale: whole pixels if
        // one was chosen. (Not in a PC window that only pretends to be a phone.)
#ifdef __ANDROID__
        n = n != 0 ? (int)fit : 0;
#else
        n = 0;
#endif
    }
    if (n == PORT_SCALE_SNAP)
        n = fit >= 1 ? (int)fit : 0;  // the biggest whole number that fits
    *scale = fit;
    if (n > 0 && fit >= 1)
        *scale = (float)(n < (int)fit ? n : (int)fit);
    if (n > 0 && fit >= 1)
    {
        // Whole picture pixels from the corner, so SDL's mouse mapping
        // (which works in picture pixels) agrees.
        *x = (float)((outW - w * (int)*scale) / (2 * (int)*scale) * (int)*scale);
        *y = (float)((outH - h * (int)*scale) / (2 * (int)*scale) * (int)*scale);
        return;
    }
    *x = floorf((outW - w * fit) / 2);
    *y = floorf((outH - h * fit) / 2);
}

// Tells SDL where the picture is (it maps the mouse and fingers with it):
// its own centered fit for FILL, the whole-number scale otherwise.
static void ApplyView(int w, int h, float x, float y, float scale)
{
    SDL_RenderSetLogicalSize(sRenderer, w, h);
    if (scale == (int)scale && fmodf(x, scale) == 0 && fmodf(y, scale) == 0)
    {
        SDL_Rect view = {(int)(x / scale), (int)(y / scale), w, h};

        SDL_RenderSetScale(sRenderer, scale, scale);
        SDL_RenderSetViewport(sRenderer, &view);
    }
}

// The window's top edge (where it can be grabbed) is on some screen, and
// enough of it to see.
static int PlaceOnScreen(const SDL_Rect *r)
{
    int i, n = SDL_GetNumVideoDisplays();

    for (i = 0; i < n; i++)
    {
        SDL_Rect screen, top = { r->x, r->y, r->w, 32 }, both;

        if (SDL_GetDisplayUsableBounds(i, &screen) == 0 && SDL_IntersectRect(&top, &screen, &both)
            && both.w >= 64 && both.h >= 16)
            return TRUE;
    }
    return FALSE;
}

// The texture the picture is copied to, w x h.
static void EnsureTexture(int w, int h)
{
    if (sTexture && sTexW == w && sTexH == h)
        return;
    if (sTexture)
        SDL_DestroyTexture(sTexture);
    sTexture = SDL_CreateTexture(sRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    sTexW = w;
    sTexH = h;
}

static void SaveWindowSettings(void)
{
    Uint32 flags;

#ifdef __ANDROID__
    return;  // always the whole screen
#endif
    if (!sWindow || getenv("JVPPBPORT_EXIT"))
        return;  // test runs leave the settings alone
    flags = SDL_GetWindowFlags(sWindow);
    PortSettingSetInt("Window", "X", sNormalRect.x);
    PortSettingSetInt("Window", "Y", sNormalRect.y);
    PortSettingSetInt("Window", "Width", sNormalRect.w);
    PortSettingSetInt("Window", "Height", sNormalRect.h);
    PortSettingSetInt("Window", "Maximized", (flags & SDL_WINDOW_MAXIMIZED) != 0);
    PortSettingSetInt("Window", "Fullscreen", (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0);
    PortSettingsSave();
}

#ifdef __ANDROID__
// Lays the touch screen out for the window's size (again when it changes):
// the texture and the logical size are the whole screen's (touch.c).
static void FitToWindow(void)
{
    int w, h;

    SDL_GetWindowSize(sWindow, &w, &h);
    PortTouchSetWindow(w, h);
    PortTouchScreenSize(&w, &h);
    SDL_RenderSetLogicalSize(sRenderer, w, h);
    EnsureTexture(w, h);
}
#endif

static void InitWindow(void)
{
    int width = PortCanvasWidth(), height = PortCanvasHeight();
    int windowWidth = width * 3, windowHeight = height * 3;
    int x = SDL_WINDOWPOS_CENTERED, y = SDL_WINDOWPOS_CENTERED;
    SDL_Rect usable, saved;
    // The hidden copies that draw the table pictures (gamedata.c) get no
    // visible window and leave the saved window place alone.
    int hidden = getenv("JVPPBPORT_TABLEDUMP") != NULL;

    // Draw at the screen's real resolution on Windows with display scaling
    // (otherwise Windows stretches the window and blurs it).
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#ifdef __ANDROID__
    // Portrait only, the system bars hidden, and Back is a game key (input.c)
    // instead of closing the app.
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "Portrait");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    // Fingers are handled by touch.c (which makes mouse events where a
    // finger works like the mouse).
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    // SDL doesn't freeze this thread when the app goes to the background:
    // the game is saved first, then waits itself (InBackground).
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
    {
        fprintf(stderr, "JVPPBPort: SDL_Init failed: %s\n", SDL_GetError());
        exit(1);
    }
#ifdef __ANDROID__
    sWindow = SDL_CreateWindow(PORT_NAME, 0, 0, 0, 0, SDL_WINDOW_FULLSCREEN);
    sRenderer = SDL_CreateRenderer(sWindow, -1, SDL_RENDERER_ACCELERATED);
    if (!sWindow || !sRenderer)
    {
        fprintf(stderr, "JVPPBPort: could not open a window: %s\n", SDL_GetError());
        exit(1);
    }
    FitToWindow();
    (void)hidden;
    return;
#else
    if (PortTouchEnabled())
    {
        // JVPPBPORT_TOUCH: the touch layout in a window of that shape.
        int w, h;

        PortTouchTestWindow(&w, &h);
        PortTouchSetWindow(w, h);
        PortTouchScreenSize(&width, &height);
        windowHeight = SDL_GetDisplayUsableBounds(0, &usable) == 0 ? usable.h * 9 / 10 : height * 2;
        windowWidth = windowHeight * width / height;
        sWindow = SDL_CreateWindow(PORT_NAME, x, y, windowWidth, windowHeight, hidden ? SDL_WINDOW_HIDDEN : 0);
        sRenderer = SDL_CreateRenderer(sWindow, -1, 0);
        SDL_RenderSetLogicalSize(sRenderer, width, height);
        EnsureTexture(width, height);
        return;
    }
#endif
    if (PortTate())
    {
        // Sideways from the start.
        width = PortCanvasHeight(), height = PortCanvasWidth();
        windowWidth = width * 3, windowHeight = height * 3;
        sPicW = width, sPicH = height;
    }
    // Default: as large as fits on the main screen (at most 3x), keeping the shape.
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0)
    {
        if (windowHeight > usable.h * 9 / 10)
        {
            windowHeight = usable.h * 9 / 10;
            windowWidth = windowHeight * width / height;
        }
        if (windowWidth > usable.w * 9 / 10)
        {
            windowWidth = usable.w * 9 / 10;
            windowHeight = windowWidth * height / width;
        }
    }
    saved.x = PortSettingGetInt("Window", "X", 0);
    saved.y = PortSettingGetInt("Window", "Y", 0);
    saved.w = PortSettingGetInt("Window", "Width", 0);
    saved.h = PortSettingGetInt("Window", "Height", 0);
    if (saved.w >= width / 2 && saved.h >= height / 2 && saved.w <= 16384 && saved.h <= 16384
        && PlaceOnScreen(&saved))
    {
        x = saved.x;
        y = saved.y;
        windowWidth = saved.w;
        windowHeight = saved.h;
    }
    if (PortScaleMode() > 0)
    {
        // A whole-number scale: that size (the saved place is kept).
        windowWidth = width * PortScaleMode();
        windowHeight = height * PortScaleMode();
    }
    sWindow = SDL_CreateWindow(PORT_NAME, x, y, windowWidth, windowHeight,
                               SDL_WINDOW_RESIZABLE | (hidden ? SDL_WINDOW_HIDDEN : 0));
    sRenderer = SDL_CreateRenderer(sWindow, -1, 0);
    if (!sWindow || !sRenderer)
    {
        fprintf(stderr, "JVPPBPort: could not open a window: %s\n", SDL_GetError());
        exit(1);
    }
    SDL_GetWindowPosition(sWindow, &sNormalRect.x, &sNormalRect.y);
    SDL_GetWindowSize(sWindow, &sNormalRect.w, &sNormalRect.h);
    // Maximized and fullscreen happen on the screen the window was put on
    // (the main screen if its old place is gone).
    if (PortSettingGetInt("Window", "Maximized", 0) && !hidden)
        SDL_MaximizeWindow(sWindow);
    if (PortSettingGetInt("Window", "Fullscreen", 0) && !hidden)
        SDL_SetWindowFullscreen(sWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_RenderSetLogicalSize(sRenderer, width, height);
    EnsureTexture(width, height);
}

// Keeps sNormalRect up to date as the window is moved or resized.
static void TrackWindow(const SDL_Event *event)
{
    if (event->type != SDL_WINDOWEVENT || IsFullscreen()
        || (SDL_GetWindowFlags(sWindow) & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED)))
        return;
    if (event->window.event == SDL_WINDOWEVENT_MOVED)
        SDL_GetWindowPosition(sWindow, &sNormalRect.x, &sNormalRect.y);
    else if (event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
        SDL_GetWindowSize(sWindow, &sNormalRect.w, &sNormalRect.h);
}

static int FrameListed(const char *list, u32 frame)
{
    while (list && *list)
    {
        if ((u32)strtoul(list, NULL, 10) == frame)
            return TRUE;
        list = strchr(list, ',');
        if (list)
            list++;
    }
    return FALSE;
}

static void SaveScreenshot(void)
{
    char name[32], path[1100];
    const uint32_t *pixels = sFrame;
    int w = PortCanvasWidth(), h = PortCanvasHeight();
    SDL_Surface *surface;

    if (PortTouchEnabled())  // the whole touch screen
    {
        pixels = PortTouchCompose(sFrame, w, h, 0, 0);
        PortTouchScreenSize(&w, &h);
    }
    surface = SDL_CreateRGBSurfaceWithFormatFrom((void *)pixels, w, h, 32, w * 4, SDL_PIXELFORMAT_ARGB8888);

    snprintf(name, sizeof(name), "shot_%u.bmp", sFrameCount);
    SDL_SaveBMP(surface, PortDataPath(path, sizeof(path), name));
    SDL_FreeSurface(surface);
}

// EXIT, the SAVE AND QUIT key: closes the program as the
// window's close button does.
void PortQuit(void)
{
    PortConfirmAbort();
    SaveWindowSettings();
    PortExit(0);
}

// Alt+Enter or F11: borderless fullscreen on the current screen, and back.
static void ToggleFullscreen(void)
{
    SDL_SetWindowFullscreen(sWindow, IsFullscreen() ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    SaveWindowSettings();
}

// Options -> SCREEN -> FULLSCREEN (PC only: touch screens are always the
// whole screen).
int PortFullscreen(void)
{
    return sWindow && !PortTouchEnabled() && IsFullscreen();
}

void PortSetFullscreen(int on)
{
    if (sWindow && !PortTouchEnabled() && !on != !IsFullscreen())
        ToggleFullscreen();
}

// The window's size and place, kept while a change may still be undone.
static SDL_Rect sRememberedRect;
static int sRememberedMaximized;

void PortWindowRemember(void)
{
    if (sWindow && !PortTouchEnabled())
    {
        sRememberedRect = sNormalRect;
        sRememberedMaximized = (SDL_GetWindowFlags(sWindow) & SDL_WINDOW_MAXIMIZED) != 0;
    }
}

void PortWindowRestore(void)
{
    if (!sWindow || PortTouchEnabled() || IsFullscreen())
        return;
    if (sRememberedMaximized)
    {
        SDL_MaximizeWindow(sWindow);
        return;
    }
    if (SDL_GetWindowFlags(sWindow) & SDL_WINDOW_MAXIMIZED)
        SDL_RestoreWindow(sWindow);
    SDL_SetWindowSize(sWindow, sRememberedRect.w, sRememberedRect.h);
    SDL_SetWindowPosition(sWindow, sRememberedRect.x, sRememberedRect.y);
}

#ifdef __ANDROID__
// The app went to the background (Home, another app, the screen turned
// off). A game in progress is saved as by the pause menu's SAVE, in case
// Android closes the app while it's away, and paused. Nothing is drawn
// until the app is back; then the save is taken back (the game goes on).
static void InBackground(void)
{
    SDL_Event event;
    int playing = gMain.mainState == STATE_GAME_MAIN && gMain.modeChangeFlags == 0;
    int saved = PortSaveGameInProgress();

    // Events that still matter once the app is back (the file picked in
    // Android's file picker comes as SDL_DROPFILE and can arrive before the
    // app is back in front, on some tablets), handed back to the game
    // then. Fingers, keys and the like are stale by then.
    SDL_Event kept[16];
    int keptCount = 0, i;

    PortRumble(0);
    PortTouchReset();
    fprintf(stderr, "JVPPBPort: in the background%s\n", saved ? " (game saved)" : "");
    while (SDL_WaitEvent(&event))
    {
        if (event.type == SDL_QUIT)
            exit(0);
        if (event.type == SDL_APP_DIDENTERFOREGROUND)
            break;
        if ((event.type == SDL_DROPFILE || event.type == SDL_WINDOWEVENT) && keptCount < (int)ARRAY_COUNT(kept))
            kept[keptCount++] = event;
        else if (event.type == SDL_DROPFILE)
            SDL_free(event.drop.file);
    }
    for (i = 0; i < keptCount; i++)
        SDL_PushEvent(&kept[i]);
    if (saved)
        PortForgetGameInProgress();
    if (playing)
        PortInputPressBack();  // comes back paused
    sNextFrameTime = 0;
    fprintf(stderr, "JVPPBPort: back\n");
}
#endif

// The next event for the game (or the setup screen), after handling the
// window's own: closing, moving/resizing, fullscreen. Returns 0 when there
// are none left.
int PortPollEvent(SDL_Event *event)
{
    while (SDL_PollEvent(event))
    {
        if (event->type == SDL_QUIT)
        {
            PortConfirmAbort();
            SaveWindowSettings();
            exit(0);
        }
        TrackWindow(event);
#ifdef __ANDROID__
        if (event->type == SDL_APP_WILLENTERBACKGROUND)
        {
            if (!sHeadless)
                InBackground();
            continue;
        }
        if (event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
            FitToWindow();
#endif
        if (!PortTouchEvent(event))
            continue;
        if (event->type == SDL_MOUSEMOTION)
            PortTateToCanvas(&event->motion.x, &event->motion.y);
        else if (event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEBUTTONUP)
            PortTateToCanvas(&event->button.x, &event->button.y);
        if (event->type == SDL_KEYDOWN && !event->key.repeat
            && (event->key.keysym.scancode == SDL_SCANCODE_F11
                || ((event->key.keysym.mod & KMOD_ALT)
                    && (event->key.keysym.scancode == SDL_SCANCODE_RETURN
                        || event->key.keysym.scancode == SDL_SCANCODE_KP_ENTER))))
        {
            ToggleFullscreen();
            continue;
        }
        return 1;
    }
    return 0;
}

// A point of the window given as fractions of its size (how SDL reports
// fingers), in the pixels of the picture shown (the canvas, or the touch
// screen's layout), also where the picture doesn't fill the window
// (split screen, odd screen shapes: it's letterboxed then).
void PortWindowToPicture(float fx, float fy, float *x, float *y)
{
    int w, h;

    SDL_GetWindowSize(sWindow, &w, &h);
    SDL_RenderWindowToLogical(sRenderer, (int)(fx * w), (int)(fy * h), x, y);
}

uint32_t gPortWindowBarColor = 0xFF000000;

// Shows a finished canvas (width x height) in the window.
void PortPresent(const uint32_t *canvas, int width, int height)
{
    static uint32_t moved[PORT_CANVAS_HEIGHT * PORT_CANVAS_WIDTH];
    int dx, dy;

    int gx = 0, gy = 0, gw = width, gh = height;  // the game's canvas in the picture
    int outW, outH;
    float x, y, scale;

    if (sHeadless)
        return;
    gPortWindowBarColor = !PortTouchEnabled() && PortWindowBarsFromEdge() ? canvas[height / 2 * width] : 0xFF000000;
    canvas = PortLcdProcess(canvas, width, height);
    PortTableWobble(&dx, &dy);
    if (PortTouchEnabled())
    {
        canvas = PortTouchCompose(canvas, width, height, dx, dy);
        PortTouchCanvasRect(&gx, &gy, &gw, &gh);
        PortTouchScreenSize(&width, &height);
    }
    else if ((dx || dy) && width * height <= PORT_CANVAS_WIDTH * PORT_CANVAS_HEIGHT)
    {
        // The table moved by dx, dy, black where it moved away from.
        int x, y;

        for (y = 0; y < height; y++)
            for (x = 0; x < width; x++)
                moved[y * width + x] = x - dx >= 0 && x - dx < width && y - dy >= 0 && y - dy < height
                                       ? canvas[(y - dy) * width + x - dx] : 0xFF000000;
        canvas = moved;
    }
    if (PortTate())
    {
        // The picture turned a quarter (see PortTate).
        static uint32_t *turned;
        int cx, cy, reverse = PortTate() == PORT_TATE_REVERSE;

        if (!turned)
            turned = malloc(PORT_MENU_MAX_W * PORT_MENU_MAX_H * sizeof(*turned));
        for (cy = 0; cy < height; cy++)
            for (cx = 0; cx < width; cx++)
                turned[reverse ? cx * height + height - 1 - cy : (width - 1 - cx) * height + cy] = canvas[cy * width + cx];
        canvas = turned;
        cx = width, width = height, height = cx;
        gw = width, gh = height;
    }
    EnsureTexture(width, height);
    SDL_UpdateTexture(sTexture, NULL, canvas, width * 4);
    sPicW = width;
    sPicH = height;
    if (SDL_GetRendererOutputSize(sRenderer, &outW, &outH) != 0 || outW <= 0 || outH <= 0)
        outW = width, outH = height;
    PictureRect(outW, outH, width, height, &x, &y, &scale);
    if (!PortLcdDraw(sRenderer, sTexture, width, height, x, y, scale, gx, gy, gw, gh))
    {
        SDL_SetRenderDrawColor(sRenderer, (gPortWindowBarColor >> 16) & 0xFF, (gPortWindowBarColor >> 8) & 0xFF,
                               gPortWindowBarColor & 0xFF, 255);
        SDL_RenderClear(sRenderer);
        ApplyView(width, height, x, y, scale);
        SDL_RenderCopy(sRenderer, sTexture, NULL, NULL);
    }
    if ((PortLcdFilter() != PORT_LCD_OFF || PortTate()) && FrameListed(sShots, sFrameCount + 1))
    {
        // Test shots: the window as shown, filter and turning included.
        char name[32], path[1100];

        snprintf(name, sizeof(name), "win_%u.bmp", sFrameCount + 1);
        PortLcdSaveWindow(sRenderer, PortDataPath(path, sizeof(path), name));
    }
    ApplyView(width, height, x, y, scale);
    SDL_RenderPresent(sRenderer);
}

// The window's Windows handle (HWND), for the file picker; NULL elsewhere.
void *PortNativeWindow(void)
{
#ifdef _WIN32
    SDL_SysWMinfo info;

    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(sWindow, &info))
        return info.info.win.window;
#endif
    return NULL;
}

// Bug snapshot (see PortBugSnapshot in testhooks.c): F12, or the frames
// listed in JVPPBPORT_SNAPSHOT (test mode).
static void BugSnapshot(void)
{
    char name[32], path[1100];
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(sFrame, PortCanvasWidth(), PortCanvasHeight(), 32,
                                                              PortCanvasWidth() * 4, SDL_PIXELFORMAT_ARGB8888);

    snprintf(name, sizeof(name), "bug_%u.bmp", sFrameCount);
    SDL_SaveBMP(surface, PortDataPath(path, sizeof(path), name));
    SDL_FreeSurface(surface);
    PortBugSnapshot(sFrameCount);
    fprintf(stderr, "JVPPBPort: bug snapshot saved as %s (and .txt, .bin)\n", path);
}

// Test hooks ask for a snapshot here (e.g. JVPPBPORT_TEST_CATCHLOG when a
// rare Pokémon shows up); taken at the next frame's start.
static int sSnapshotRequested;
void PortRequestSnapshot(void)
{
    sSnapshotRequested = 1;
}

static void HandleEvents(void)
{
    SDL_Event event;

    if (FrameListed(getenv("JVPPBPORT_SNAPSHOT"), sFrameCount) || sSnapshotRequested)
    {
        sSnapshotRequested = 0;
        BugSnapshot();
    }
    while (!sHeadless && PortPollEvent(&event))
    {
        if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_F12)
        {
            BugSnapshot();
            continue;
        }
        PortInputEvent(&event);
    }
    REG_KEYINPUT = KEYS_MASK & ~PortReadInput(sFrameCount);  // the GBA register reads 0 for "pressed"
}

static void WaitForNextFrame(void)
{
    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 frameTicks = (Uint64)(freq / GBA_FPS);
    Uint64 now = SDL_GetPerformanceCounter();

    if (sNextFrameTime == 0 || now > sNextFrameTime + frameTicks * 4)
        sNextFrameTime = now;  // first frame, or we fell far behind: resync
    sNextFrameTime += frameTicks;
    while ((now = SDL_GetPerformanceCounter()) < sNextFrameTime)
    {
        Uint64 msLeft = (sNextFrameTime - now) * 1000 / freq;
        if (msLeft > 1)
            SDL_Delay((Uint32)(msLeft - 1));
    }
}

static int InterruptEnabled(u16 flag)
{
    return (IO16(REG_OFFSET_IME) & 1) && (IO16(REG_OFFSET_IE) & flag);
}

void PortVBlankIntrWait(void)
{
    int line;

    // Draw the visible lines. The game's VCount handler changes registers
    // partway down the screen (shop panel, cutscene bars), so it runs at its
    // chosen line, after that line is drawn (it waits for HBlank first).
    PortRenderBegin(sFrame);
    for (line = 0; line < PORT_SCREEN_HEIGHT; line++)
    {
        u16 dispstat = IO16(REG_OFFSET_DISPSTAT) & ~(DISPSTAT_VBLANK | DISPSTAT_HBLANK | DISPSTAT_VCOUNT);

        IO16(REG_OFFSET_VCOUNT) = line;
        IO16(REG_OFFSET_DISPSTAT) = dispstat;
        PortRenderScanline(line);

        if ((dispstat >> 8) == line)
        {
            IO16(REG_OFFSET_DISPSTAT) = dispstat | DISPSTAT_VCOUNT | DISPSTAT_HBLANK;
            if ((dispstat & DISPSTAT_VCOUNT_INTR) && InterruptEnabled(INTR_FLAG_VCOUNT) && *gVCountIntrFuncPtr)
                (*gVCountIntrFuncPtr)();
        }
    }

    PortRenderEnd();
    PortDrawTexts(sFrame);
    PortConfirmFrame();
    PortConfirmDraw(sFrame);
    PortPresent(sFrame, PortCanvasWidth(), PortCanvasHeight());

    sFrameCount++;
    PortTestHooks(sFrameCount);
    // Test mode: swap the field of the game that is starting, during Field
    // Select's fade-out (its state 2): the choice is final by then, and the
    // board hasn't started loading. These gMain members are near its start,
    // where the PC and GBA layouts agree.
    if (sTestField >= 0 && gMain.mainState == STATE_FIELD_SELECT && gMain.subState == 2)
    {
        gMain.tempField = gMain.selectedField = sTestField;
        gMain.isBonusField = sTestField >= FIELD_BONUS_START;
        gMain.continueFromSave = FALSE;
        sTestField = -1;
    }
    if (FrameListed(sShots, sFrameCount))
        SaveScreenshot();
    if (sExitFrame && sFrameCount >= sExitFrame)
        PortExit(0);

    HandleEvents();
    if (!sFast)
        WaitForNextFrame();

    PortAudioFrame();

    IO16(REG_OFFSET_VCOUNT) = PORT_SCREEN_HEIGHT;
    IO16(REG_OFFSET_DISPSTAT) = (IO16(REG_OFFSET_DISPSTAT) & ~(DISPSTAT_HBLANK | DISPSTAT_VCOUNT)) | DISPSTAT_VBLANK;
    if ((IO16(REG_OFFSET_DISPSTAT) & DISPSTAT_VBLANK_INTR) && InterruptEnabled(INTR_FLAG_VBLANK) && *gVBlankIntrFuncPtr)
        (*gVBlankIntrFuncPtr)();
}

uint32_t PortCurrentFrame(void)
{
    return sFrameCount;
}

void PortSoftReset(void)
{
    longjmp(sResetJump, 1);
}

int main(int argc, char **argv)
{
#ifdef __ANDROID__
    if (!gPortForkedCopy)
        PortAndroidTestEnv();
#endif
    sShots = getenv("JVPPBPORT_SHOTS");
    sFast = getenv("JVPPBPORT_FAST") != NULL;
    sTestField = getenv("JVPPBPORT_TEST_FIELD") ? (int)strtol(getenv("JVPPBPORT_TEST_FIELD"), NULL, 10) : -1;
    sExitFrame = getenv("JVPPBPORT_EXIT") ? strtoul(getenv("JVPPBPORT_EXIT"), NULL, 10) : 0;
    gPortStaticView = getenv("JVPPBPORT_GBAVIEW") == NULL;
    PortSystemInit();
    if (!gPortForkedCopy)
        fprintf(stderr, "%s %s\n", PORT_NAME, PORT_VERSION);
    sHeadless = gPortForkedCopy;
    if (!sHeadless)
        InitWindow();
    PortSetup();
    PortInputInit();
    PortAudioInit(sFast);
    REG_KEYINPUT = KEYS_MASK;
    setjmp(sResetJump);
    AgbMain();
    return 0;
}

// The start of a copy of the program made with fork() (gamedata.c, after
// setting its JVPPBPORT_ settings): runs the game again from the start,
// without a window. Leaves with PortExit.
void PortRunForkedCopy(void)
{
    gPortForkedCopy = 1;
    PortSettingsForget();
    PortSaveForget();
    main(0, NULL);
    PortExit(1);
}
