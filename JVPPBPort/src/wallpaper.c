// JVPPBPort: a picture of the player's own behind the game, fullscreen or
// windowed (PC only, 0.3.0; GitHub issue #2).
//
// Shown when a file named wallpaper.png is in the data folder (system.c:
// %APPDATA%\JVPPBPort, or the program's own folder in portable mode), in
// the bars beside the game: fullscreen, or a window whose shape differs from
// the game's (e.g. maximized). Without the file the bars stay as they always
// were.
//
// - "Fit", not "Fill": the whole picture, its shape kept, as big as the
//   window or screen allows, centered; black where it doesn't reach. Nothing
//   cropped or stretched. The game is drawn over it exactly where it always is.
// - With TATE MODE on, the wallpaper turns with the game (same direction),
//   so it's upright on the monitor stood on its side; the fit is worked out
//   on the turned screen.
// - Read at start, and again when fullscreen is switched or the window gets
//   the focus back (so a picture swapped in Explorer shows on clicking the
//   game); the texture is only made again when the file's bytes changed. A
//   missing, broken or huge file (over MAX_SIDE px a side) is logged and
//   ignored.
// - Its own texture, smoothed (linear) on its own (SDL_SetTextureScaleMode);
//   the game's texture and SDL's scale-quality hint are left alone.
//
// PNG decoding: stb_image.h (public domain / MIT, see its top), PNG only.
#ifndef __ANDROID__
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "global.h"
#include "jvppbport.h"

#define MAX_SIDE 8192
#define MAX_FILE_BYTES (256 * 1024 * 1024)

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO              // the file is read with SDL (UTF-8 paths on Windows)
#define STBI_MAX_DIMENSIONS MAX_SIDE
#include "stb_image.h"

static SDL_Texture *sWallpaper;
static SDL_Renderer *sWallpaperRenderer;  // the renderer sWallpaper belongs to
static int sWallW, sWallH;                // the texture's size
static int sWasOn;
static int sRecheck;                     // read the file again (PortWallpaperRecheck)
static int sShown;                       // drawn this frame (PortWallpaperUpdate)
static Uint64 sFileHash;                  // of the file the texture was made from (0 = none)
static Uint64 sFileSize;
static char sLastMessage[160];            // logged once until something changes

static void Log(const char *message)
{
    if (strcmp(message, sLastMessage) == 0)
        return;
    snprintf(sLastMessage, sizeof(sLastMessage), "%s", message);
    fprintf(stderr, "JVPPBPort: %s\n", message);
}

static void Forget(void)
{
    if (sWallpaper)
        SDL_DestroyTexture(sWallpaper);
    sWallpaper = NULL;
    sWallpaperRenderer = NULL;
    sFileHash = 0;
    sFileSize = 0;
}

// The whole file, or NULL (*size 0 if it isn't there).
static unsigned char *ReadFile(const char *path, Sint64 *size)
{
    SDL_RWops *rw = SDL_RWFromFile(path, "rb");
    unsigned char *data;

    *size = 0;
    if (!rw)
        return NULL;
    *size = SDL_RWsize(rw);
    if (*size <= 0 || *size > MAX_FILE_BYTES)
    {
        SDL_RWclose(rw);
        if (*size <= 0)
            *size = -1;  // there, but empty or unreadable
        return NULL;
    }
    data = malloc((size_t)*size);
    if (data && SDL_RWread(rw, data, 1, (size_t)*size) != (size_t)*size)
    {
        free(data);
        data = NULL;
    }
    SDL_RWclose(rw);
    return data;
}

static Uint64 Hash(const unsigned char *data, Sint64 size)
{
    Uint64 h = 1469598103934665603ULL;  // FNV-1a
    Sint64 i;

    for (i = 0; i < size; i++)
        h = (h ^ data[i]) * 1099511628211ULL;
    return h ? h : 1;
}

// Halves an RGBA picture (each new pixel the average of 2x2 old ones).
static unsigned char *Halve(unsigned char *pixels, int *w, int *h)
{
    int nw = *w / 2 > 0 ? *w / 2 : 1, nh = *h / 2 > 0 ? *h / 2 : 1, x, y, c;
    unsigned char *out = malloc((size_t)nw * nh * 4);

    if (!out)
        return pixels;
    for (y = 0; y < nh; y++)
        for (x = 0; x < nw; x++)
            for (c = 0; c < 4; c++)
            {
                int x0 = x * 2, y0 = y * 2;
                int x1 = x0 + 1 < *w ? x0 + 1 : x0, y1 = y0 + 1 < *h ? y0 + 1 : y0;

                out[(y * nw + x) * 4 + c] = (pixels[(y0 * *w + x0) * 4 + c] + pixels[(y0 * *w + x1) * 4 + c]
                                             + pixels[(y1 * *w + x0) * 4 + c] + pixels[(y1 * *w + x1) * 4 + c] + 2) / 4;
            }
    stbi_image_free(pixels);
    *w = nw;
    *h = nh;
    return out;
}

static void Load(SDL_Renderer *renderer, int outW, int outH)
{
    char path[1100], message[160];
    Sint64 size;
    unsigned char *file, *pixels;
    int w, h, n, maxW = MAX_SIDE, maxH = MAX_SIDE, screen = outW > outH ? outW : outH;
    Uint32 sideW, sideH;
    Uint64 hash;
    SDL_RendererInfo info;
    SDL_Texture *texture;
    SDL_Window *window = SDL_RenderGetWindow(renderer);
    SDL_DisplayMode desktop;

    // Measured against the monitor, not the window, so a picture loaded in a
    // small window is still sharp once the window is maximized or fullscreen.
    if (window && SDL_GetDesktopDisplayMode(SDL_GetWindowDisplayIndex(window), &desktop) == 0)
    {
        if (desktop.w > screen)
            screen = desktop.w;
        if (desktop.h > screen)
            screen = desktop.h;
    }

    PortDataPath(path, sizeof(path), "wallpaper.png");
    file = ReadFile(path, &size);
    if (!file)
    {
        Forget();
        if (size == 0)
            Log("no wallpaper.png in the data folder (black bars)");
        else if (size > MAX_FILE_BYTES)
            Log("wallpaper.png not loaded: the file is over 256 MB");
        else
            Log("wallpaper.png not loaded: the file could not be read");
        return;
    }
    hash = Hash(file, size);
    if (sWallpaper && sWallpaperRenderer == renderer && hash == sFileHash && (Uint64)size == sFileSize)
    {
        free(file);  // the same picture as last time
        return;
    }
    Forget();
    // The size from the PNG's header (its first chunk, IHDR), before decoding.
    if (size < 24 || memcmp(file, "\x89PNG\r\n\x1a\n", 8) != 0 || memcmp(file + 12, "IHDR", 4) != 0)
    {
        Log("wallpaper.png not loaded: not a PNG file");
        free(file);
        return;
    }
    sideW = (Uint32)file[16] << 24 | (Uint32)file[17] << 16 | (Uint32)file[18] << 8 | file[19];
    sideH = (Uint32)file[20] << 24 | (Uint32)file[21] << 16 | (Uint32)file[22] << 8 | file[23];
    if (sideW > MAX_SIDE || sideH > MAX_SIDE)
    {
        snprintf(message, sizeof(message), "wallpaper.png not loaded: %ux%u is too big (at most %d a side)",
                 (unsigned)sideW, (unsigned)sideH, MAX_SIDE);
        Log(message);
        free(file);
        return;
    }
    pixels = stbi_load_from_memory(file, (int)size, &w, &h, &n, 4);
    free(file);
    if (!pixels)
    {
        snprintf(message, sizeof(message), "wallpaper.png not loaded: broken PNG (%s)", stbi_failure_reason());
        Log(message);
        return;
    }
    snprintf(message, sizeof(message), "wallpaper %dx%d loaded", w, h);
    // Smaller copies for pictures far bigger than the screen (smoothing a
    // picture down to under half its size skips pixels) or than the
    // graphics card's textures can be.
    if (SDL_GetRendererInfo(renderer, &info) == 0 && info.max_texture_width > 0)
        maxW = info.max_texture_width, maxH = info.max_texture_height;
    while ((w > maxW || h > maxH || (w >= 2 * screen && h >= 2 * screen)) && (w > 1 || h > 1))
        pixels = Halve(pixels, &w, &h);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!texture || SDL_UpdateTexture(texture, NULL, pixels, w * 4) != 0)
    {
        snprintf(message, sizeof(message), "wallpaper.png not loaded: no texture (%s)", SDL_GetError());
        Log(message);
        if (texture)
            SDL_DestroyTexture(texture);
        stbi_image_free(pixels);
        return;
    }
    stbi_image_free(pixels);
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);  // see-through parts show black
    sWallpaper = texture;
    sWallpaperRenderer = renderer;
    sWallW = w;
    sWallH = h;
    sFileHash = hash;
    sFileSize = (Uint64)size;
    sLastMessage[0] = 0;  // log it each time a new picture is loaded
    Log(message);
}

// Each frame, before drawing: is there a wallpaper to draw this frame?
// on = the PC window (not touch screens). Read at start and after
// PortWallpaperRecheck.
int PortWallpaperUpdate(SDL_Renderer *renderer, int on, int outW, int outH)
{
    if (on && (!sWasOn || sRecheck))
        Load(renderer, outW, outH);
    sWasOn = on;
    sRecheck = 0;
    sShown = on && sWallpaper && sWallpaperRenderer == renderer;
    return sShown;
}

// Fullscreen switched, or the window got the focus back: read the file
// again before the next frame (only remade if it changed).
void PortWallpaperRecheck(void)
{
    sRecheck = 1;
}

int PortWallpaperShown(void)
{
    return sShown;
}

// Draws it over the cleared window (black) and makes the picture's place
// black again, so the game goes on top just as over the plain bars.
// picture = where the game goes, in window pixels.
void PortWallpaperDraw(SDL_Renderer *renderer, float px, float py, float pw, float ph)
{
    int outW, outH, logicalW, logicalH, tate = PortTate();
    float sw, sh, s;
    SDL_FRect dst, game = {px, py, pw, ph};

    if (!sShown || SDL_GetRendererOutputSize(renderer, &outW, &outH) != 0)
        return;
    SDL_RenderGetLogicalSize(renderer, &logicalW, &logicalH);
    SDL_RenderSetLogicalSize(renderer, 0, 0);  // window pixels
    // Its size on the screen as seen: turned a quarter with Tate.
    sw = (float)(tate ? sWallH : sWallW);
    sh = (float)(tate ? sWallW : sWallH);
    s = outW / sw < outH / sh ? outW / sw : outH / sh;
    dst.w = roundf(sWallW * s);
    dst.h = roundf(sWallH * s);
    dst.x = floorf((outW - dst.w) / 2);
    dst.y = floorf((outH - dst.h) / 2);
    // Tate ON puts the game's top at the window's left (a quarter turn
    // anticlockwise, platform.c), REVERSE at the right. SDL turns clockwise
    // around the rectangle's middle.
    SDL_RenderCopyExF(renderer, sWallpaper, NULL, &dst,
                      tate == PORT_TATE_ON ? -90.0 : tate == PORT_TATE_REVERSE ? 90.0 : 0.0, NULL, SDL_FLIP_NONE);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderFillRectF(renderer, &game);
    if (logicalW > 0 && logicalH > 0)
        SDL_RenderSetLogicalSize(renderer, logicalW, logicalH);
}
#endif
