// JVPPBPort: the first-start setup screen. The program contains none of the
// game's data (see gamedata.c), so the first time it starts it asks for the
// player's ROM: a button opens a file picker (Windows' and Android's own;
// zenity on Linux), or the file can be dropped onto the window. Once the ROM checks out, its
// data is kept in the asset folder and the table pictures are made (a few
// seconds), and the game starts. Later starts skip all this.
//
// Runs in the game's window before the game starts, so the window's size,
// place and fullscreen (and Alt+Enter / F11) work as usual.
//
// JVPPBPORT_ROM=file: take the ROM from that file without asking (tests).
// The Linux development build also uses ./baserom.gba if it's there.
// Test mode: with JVPPBPORT_SHOTS set, the screen asking for the ROM is
// saved as setup.bmp in the data folder; with JVPPBPORT_EXIT set too, the
// program then quits.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif
#include "jvppbport.h"

// The screen is drawn at the menu size (the screen's shape on a touch
// screen, see ppu.c), its contents in the middle.
#define W sW
#define H sH

#define COLOR_BACK 0xFF000000  // black, like the game's own screens
#define COLOR_TEXT 0xFFFFFFFF
#define COLOR_DIM 0xFF9AA5BD
#define COLOR_TITLE 0xFFFFD040
#define COLOR_ERROR 0xFFFF6060
#define COLOR_BUTTON 0xFF2F5FB0
#define COLOR_BUTTON_HOVER 0xFF4A7FD8

static int sW = PORT_CANVAS_WIDTH, sH = PORT_CANVAS_HEIGHT;
static int sOY;  // how far the contents are moved down
static SDL_Rect sButton = { 28, 296, 200, 34 };

static uint32_t sCanvas[PORT_MENU_MAX_W * PORT_MENU_MAX_H];

static void Measure(void)
{
    PortMenuSize(&sW, &sH);
    sOY = (sH - PORT_CANVAS_HEIGHT) / 2;
    sButton.x = (sW - sButton.w) / 2;
    sButton.y = 296 + sOY;
}
static const char *sStatus;
static uint32_t sStatusColor;
static int sHover;

static void FillBox(int x, int y, int w, int h, uint32_t color)
{
    int i, j;

    y += sOY;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            if (i >= 0 && i < W && j >= 0 && j < H)
                sCanvas[j * W + i] = color;
}

static void CenterText(int y, const char *s, uint32_t color, int scale)
{
    PortDrawTextOn(sCanvas, W, H, (W - PortTextWidth(s) * scale) / 2, y + sOY, s, color, scale);
}

// The program's ball logo (drawn, like its icon): red over blue, for Ruby and Sapphire.
static void DrawBall(int cx, int cy, int r)
{
    int x, y;

    for (y = -r; y <= r; y++)
    {
        for (x = -r; x <= r; x++)
        {
            int d2 = x * x + y * y;
            uint32_t color;

            if (d2 > r * r)
                continue;
            if (d2 > (r - 3) * (r - 3) || (y >= -2 && y <= 2) || d2 <= 81)
                color = 0xFF101010;
            else
                color = y < 0 ? 0xFFE03838 : 0xFF245CCC;  // Ruby red, Sapphire blue
            if (d2 <= 30)
                color = 0xFFF4F4F4;
            sCanvas[(cy + sOY + y) * W + cx + x] = color;
        }
    }
}

// preparing: the "making the table pictures" version, with a moving bar.
static void Draw(int preparing)
{
    int i;

    for (i = 0; i < W * H; i++)
        sCanvas[i] = COLOR_BACK;
    DrawBall(W / 2, 80, 32);
    CenterText(130, "JONNISVIBES", COLOR_TEXT, 2);
    CenterText(150, "PPB PORT", COLOR_TEXT, 3);
    CenterText(184, "A FULL-TABLE PORT OF", COLOR_DIM, 1);
    CenterText(196, "PPB RUBY & SAPPHIRE (GBA)", COLOR_DIM, 1);
    if (preparing)
    {
        int pos = (SDL_GetTicks() / 8) % (sButton.w + 40) - 40;

        CenterText(240, "PREPARING THE TABLES...", COLOR_TITLE, 1);
        FillBox(sButton.x - 2, 306, sButton.w + 4, 14, COLOR_DIM);
        FillBox(sButton.x, 308, sButton.w, 10, COLOR_BACK);
        FillBox(sButton.x + (pos < 0 ? 0 : pos), 308, pos < 0 ? 40 + pos : (pos + 40 > sButton.w ? sButton.w - pos : 40),
                 10, COLOR_BUTTON_HOVER);
        CenterText(420, "THIS TAKES A FEW SECONDS.", COLOR_DIM, 1);
        return;
    }
    CenterText(226, "FIRST-TIME SETUP", COLOR_TITLE, 1);
    CenterText(248, "THIS PROGRAM DOESN'T CONTAIN THE GAME.", COLOR_TEXT, 1);
    CenterText(260, "IT NEEDS YOUR OWN ROM OF", COLOR_TEXT, 1);
    CenterText(272, "PPB RUBY & SAPPHIRE (USA, .GBA).", COLOR_TEXT, 1);
    FillBox(sButton.x, sButton.y - sOY, sButton.w, sButton.h, sHover ? COLOR_BUTTON_HOVER : COLOR_BUTTON);
    CenterText(sButton.y - sOY + 10, "CHOOSE ROM FILE", COLOR_TEXT, 2);
#ifdef __ANDROID__
    CenterText(342, "THE FILE MUST BE ON THIS DEVICE.", COLOR_DIM, 1);
#else
    CenterText(342, "OR DROP THE FILE ON THIS WINDOW.", COLOR_DIM, 1);
#endif
    if (sStatus)
        CenterText(372, sStatus, sStatusColor, 1);
    CenterText(420, "ONLY NEEDED ONCE.", COLOR_DIM, 1);
}

static void Show(int preparing)
{
    static uint32_t small[PORT_SCREEN_WIDTH * PORT_SCREEN_HEIGHT];
    int w = PORT_SCREEN_WIDTH, h = PORT_SCREEN_HEIGHT, x, y;

    Measure();
    Draw(preparing);
    if (gPortStaticView)
    {
        PortPresent(sCanvas, W, H);
        return;
    }
    // The GBA-sized window (JVPPBPORT_GBAVIEW): shrunk to fit.
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            small[y * w + x] = sCanvas[(y * H / h) * W + x * W / w];
    PortPresent(small, w, h);
}

// Takes the game's data from the ROM at `path`. Returns 1 if the game
// can start.
static int TryRom(const char *path)
{
    const char *problem;

    sStatus = "CHECKING THE FILE...";
    sStatusColor = COLOR_TEXT;
    Show(0);
    problem = PortGameDataFromRom(path);
    if (!problem && !PortGameDataLoad())
        problem = "CAN'T READ THE SAVED GAME DATA.";
    sStatus = problem;
    sStatusColor = COLOR_ERROR;
    return problem == NULL;
}

// Asks for a file with the system's file picker. Returns 1 if the game
// can start.
static int ChooseFile(void)
{
#ifdef _WIN32
    char path[MAX_PATH] = "";
    OPENFILENAMEA ofn = { sizeof(ofn) };

    ofn.hwndOwner = (HWND)PortNativeWindow();
    ofn.lpstrFilter = "Game Boy Advance ROM (*.gba)\0*.gba\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = sizeof(path);
    ofn.lpstrTitle = "Choose your PPB Ruby & Sapphire ROM";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!GetOpenFileNameA(&ofn))
        return 0;
    return TryRom(path);
#elif defined(__ANDROID__)
    // The answer comes later, as a dropped file (see android.c).
    sStatus = NULL;
    PortAndroidPickRom();
    return 0;
#else
    char path[1100] = "";
    FILE *p = popen("zenity --file-selection --title='Choose your PPB Ruby & Sapphire ROM' "
                    "--file-filter='GBA ROM | *.gba' --file-filter='All files | *' 2>/dev/null", "r");
    int status;

    if (!p)
        return 0;
    if (!fgets(path, sizeof(path), p))
        path[0] = '\0';
    status = pclose(p);
    path[strcspn(path, "\n")] = '\0';
    if (path[0])
        return TryRom(path);
    if (status != 0 && WEXITSTATUS(status) == 127)
    {
        sStatus = "NO FILE PICKER HERE. DROP THE FILE.";
        sStatusColor = COLOR_ERROR;
    }
    return 0;
#endif
}

static void SaveShot(void)
{
    static int saved;
    char path[1100];
    SDL_Surface *surface;

    if (saved++)
        return;
    surface = SDL_CreateRGBSurfaceWithFormatFrom(sCanvas, W, H, 32, W * 4, SDL_PIXELFORMAT_ARGB8888);
    SDL_SaveBMP(surface, PortDataPath(path, sizeof(path), "setup.bmp"));
    SDL_FreeSurface(surface);
    if (getenv("JVPPBPORT_EXIT"))
        exit(0);
}

static int InButton(int x, int y)
{
    SDL_Point p = { x, y };
    return SDL_PointInRect(&p, &sButton);
}

static void AskForRom(void)
{
    Measure();
    sStatus = PortGameDataProblem();
    sStatusColor = COLOR_ERROR;
    for (;;)
    {
        SDL_Event event;
        int ready = 0;

        while (!ready && PortPollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_DROPFILE:
                ready = TryRom(event.drop.file);
#ifdef __ANDROID__
                PortAndroidPickedFileDone(event.drop.file);
#endif
                SDL_free(event.drop.file);
                break;
            case SDL_MOUSEMOTION:
                sHover = InButton(event.motion.x, event.motion.y);
                break;
            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT && InButton(event.button.x, event.button.y))
                    ready = ChooseFile();
                break;
            case SDL_KEYDOWN:
                if (!event.key.repeat && !(event.key.keysym.mod & KMOD_ALT)
                    && (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_SPACE))
                    ready = ChooseFile();
                break;
            }
        }
        if (ready)
            return;
        Show(0);
        if (getenv("JVPPBPORT_SHOTS"))
            SaveShot();
        SDL_Delay(16);
    }
}

static void MakeTables(void)
{
    int result;

    if (!PortTablesStart())
        return;
    while ((result = PortTablesPoll()) == 0)
    {
        SDL_Event event;

        while (PortPollEvent(&event))
            ;
        Show(1);
        SDL_Delay(16);
    }
    if (result < 0)
        fprintf(stderr, "JVPPBPort: no table pictures this time (Field Select and bonus stages show without them)\n");
}

// Gets the game's data in place (asking for the ROM if needed) and the
// table pictures made. Returns when the game can start.
void PortSetup(void)
{
    int tableJob = getenv("JVPPBPORT_TABLEDUMP") != NULL;  // a hidden copy making a table picture
    int loaded = PortGameDataLoad();

    if (!loaded && !tableJob)
    {
        const char *rom = getenv("JVPPBPORT_ROM");

#if !defined(_WIN32) && !defined(__ANDROID__)
        if ((!rom || !*rom) && access("baserom.gba", R_OK) == 0)
            rom = "baserom.gba";
#endif
        if (rom && *rom)
        {
            const char *problem = PortGameDataFromRom(rom);
            if (problem)
                fprintf(stderr, "JVPPBPort: %s: %s\n", rom, problem);
            else
                loaded = PortGameDataLoad();
        }
        if (!loaded)
            AskForRom();
    }
    if (tableJob)
    {
        if (!loaded)
            PortExit(1);
        return;
    }
    if (!PortTablesLoad())
        MakeTables();
}
