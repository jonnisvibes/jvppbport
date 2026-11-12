// JVPPBPort: the static table view's side of the game. The renderer
// (ppu.c) draws the whole Ruby/Sapphire board at once, so sprites that belong
// to the table must be drawn at their place on the table, not at a place on
// the GBA's 240x160 screen.
//
// How sprites get there:
//  - While compiling, JVPPBPort/tools/portrewrite.py makes the game's drawing
//    code read the camera through PortDrawCam_*(). In the static view these
//    return a pretend camera at -2048, so a sprite that follows the table gets
//    the position (table position + 2048). Sprites the game hides (it moves
//    them to y = 200 or so) and sprites fixed to the screen (banners, pause
//    panel) keep small positions. The real camera keeps running untouched,
//    so the game plays exactly as before.
//  - The same rewrite sends every sprite x/y write through PortOamSetX/Y,
//    which remember the full value. The GBA keeps only 8 bits of y and 9 of x.
//  - When the game copies its sprite list to OAM (once per frame), the
//    snapshot below sorts each sprite: both coordinates big = on the table;
//    one big = hidden; neither = on the GBA screen.
//  - Objects the game only shows near the camera are switched on
//    (PORT_SHOW_ALL in include/global.h).
//
// The board background: the main boards keep only 22 of their 56 rows of
// tiles in VRAM (board row r in VRAM row slot (r + 10) % 22), and swap rows in
// as the camera scrolls. Lit-up board graphics (arrows, the H-O-L-E letters,
// the Saver sign...) are drawn by copying tiles into those VRAM rows. To show
// all 56 rows, the port keeps its own copy of each row (sRowTiles): row loads
// and lit-up graphics copied into VRAM also go to the right row there
// (PortStreamedWrite; the row of each lit-up graphic comes from the data
// tables, see JVPPBPort/tools/wraprows.py), and ppu.c draws from it.
//
// JVPPBPORT_GBAVIEW=1 turns all of this off (gPortStaticView = 0).
#include "jvppbport_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include "global.h"
#include "main.h"
#include "constants/fields.h"
#include "constants/board/center_screen_states.h"
#include "constants/board/main_board.h"
#include "constants/palette_mappings.h"
#include "constants/sprite_groups.h"
#include "jvppbport.h"

#define WORLD_BIAS 2048

int gPortStaticView;
struct PortSprite gPortSprites[PORT_MAX_SPRITES];
int gPortSpriteCount;
uint16_t gPortSpritePalettes[PORT_MAX_SPRITES][16];

extern const Palette gFieldVariant_Pals[][6];

static s16 sFullX[PORT_MAX_SPRITES], sFullY[PORT_MAX_SPRITES];
static u8 sHasFullX[PORT_MAX_SPRITES], sHasFullY[PORT_MAX_SPRITES];
static int sLoadedCount = 128;

#define BOARD_ROWS 56
#define BOARD_SLOTS 22
#define ROW_BYTES 0x400
#define STREAMED_VRAM 0x8000  // char block 2
extern u8 gBoardBGTileBuffer[];
extern u8 gBoardBGTileBufferAlt[];
static u8 sRowTiles[BOARD_ROWS][ROW_BYTES];
static u8 sRowCopied[BOARD_ROWS];
// The lit-up graphics last written into each row (where sLitSet is set).
// The game reloads a row's plain tiles when the camera scrolls onto it and
// redraws its lights a frame later; on the GBA that row is at the screen's
// edge, here it's mid-table, so the lights are put back right away.
static u8 sLit[BOARD_ROWS][ROW_BYTES];
static u8 sLitSet[BOARD_ROWS][ROW_BYTES];
static s8 sSlotRow[BOARD_SLOTS];  // board row last loaded into each VRAM slot
static struct SpriteGroup *sOwner[PORT_MAX_SPRITES];
// The owning group's base when each sprite's x/y was written. Some code
// reuses one group for several objects (the Ruby pond's three bumpers move
// the base before drawing each one), so its base at the end of the frame
// only fits the last object.
static int sRefX[PORT_MAX_SPRITES], sRefY[PORT_MAX_SPRITES];
static u8 sHasRefX[PORT_MAX_SPRITES], sHasRefY[PORT_MAX_SPRITES];

_Static_assert(sizeof(struct OamData) == 8, "struct OamData changed size");

// A Ruby or Sapphire board is being played (or demoed) in the static view.
int PortTableView(void)
{
    return gPortStaticView
        && (gMain.mainState == STATE_GAME_MAIN || gMain.mainState == STATE_GAME_IDLE)
        && gMain.selectedField < MAIN_FIELD_COUNT;
}

// The pretend camera. Each field gets the value that makes the formulas the
// game uses with it come out as (table position + 2048): objects use
// `pos - cameraXOffset`; the ball uses `pos - cameraBaseY - cameraScrollOffset
// - cameraYAdjust` (and `- screenShakeX` for x).
//
// A full-size bonus stage (BONUS STAGE FULL) has a still camera: the formulas
// must come out as `pos - camera`, without the shake and the cameraYAdjust
// the stages use for their camera moves.
static int BonusFullShown(void)
{
    return PortBonusFull() && (gMain.mainState == STATE_GAME_MAIN || gMain.mainState == STATE_GAME_IDLE)
        && gMain.selectedField >= FIELD_BONUS_START;
}

int PortDrawCam_cameraXOffset(int value)
{
    return PortTableView() ? -WORLD_BIAS : value;
}

int PortDrawCam_cameraYOffset(int value)
{
    // FULL's held camera, also before the stage's first camera update (the
    // fade in, when the game's start camera is still set: Spheal's Spheals).
    if (BonusFullShown())
        return gBoardConfig.fieldLayout.cameraScrollMinY - 80 + PortBonusIntroAdjust(gCurrentPinballGame->cameraYAdjust);
    return PortTableView() ? -WORLD_BIAS : value;
}

int PortDrawCam_cameraBaseX(int value)
{
    if (BonusFullShown())
        return value - gCurrentPinballGame->screenShakeX;
    return PortTableView() ? -WORLD_BIAS - gCurrentPinballGame->screenShakeX : value;
}

int PortDrawCam_cameraBaseY(int value)
{
    if (BonusFullShown())
        return value - gCurrentPinballGame->cameraScrollOffset - gCurrentPinballGame->cameraYAdjust
            + PortBonusIntroAdjust(gCurrentPinballGame->cameraYAdjust);
    if (!PortTableView())
        return value;
    return -WORLD_BIAS - gCurrentPinballGame->cameraScrollOffset - gCurrentPinballGame->cameraYAdjust;
}

// Tilt and screen shake: the game shows them by moving its camera, which
// the static view doesn't follow, so the whole table is drawn moved the
// other way instead: a small wobble of the whole table.
// The offset in pixels to draw the table at; 0, 0 outside the table view.
void PortTableWobble(int *dx, int *dy)
{
    *dx = *dy = 0;
    if (!PortTableView() && !BonusFullShown())
        return;
    *dx = -(gCurrentPinballGame->tiltXOffset + gCurrentPinballGame->screenShakeX);
    *dy = -(gCurrentPinballGame->tiltYOffset + gCurrentPinballGame->screenShakeY);
}

// Where the GBA screen (BG0: score row, banners, panels; and screen-fixed
// sprites) goes on the table, as a board position of its top-left corner.
// Normally a fixed spot in the middle of the table. While the game frames a
// scene on the board (the ball held in the center hole, a Pokémon being
// caught, or Latios bringing the ball back with the Ball Saver), the screen
// goes where the game's camera is, because the scene's screen parts (the
// dark band, the text, Latios) must line up with its table parts (the
// Pokémon's portrait over the hole, the ball). It slides between the two.
#define POPUP_SPOT_X ((PORT_TABLE_WIDTH - PORT_SCREEN_WIDTH) / 2)
#define POPUP_SPOT_Y ((PORT_TABLE_HEIGHT - PORT_SCREEN_HEIGHT) / 2)
#define POPUP_SLIDE_SPEED 8  // pixels per frame

static int SlideTowards(int from, int to)
{
    if (to > from + POPUP_SLIDE_SPEED)
        return from + POPUP_SLIDE_SPEED;
    if (to < from - POPUP_SLIDE_SPEED)
        return from - POPUP_SLIDE_SPEED;
    return to;
}

// `snap`: jump straight there (the first frame of a table after another screen).
void PortPopupWindow(int cameraX, int cameraY, int snap, int *x, int *y)
{
    static int curX, curY;
    int framed = gCurrentPinballGame->ballCatchState == TRAP_CENTER_HOLE
              || gCurrentPinballGame->captureState == MON_CAPTURE_SPECIAL_STATE_CAPTURE_CUTSCENE
              || (gMain.modeChangeFlags & MODE_CHANGE_BALL_SAVER);
    int targetX = framed ? cameraX : POPUP_SPOT_X;
    int targetY = framed ? cameraY : POPUP_SPOT_Y;

    curX = snap ? targetX : SlideTowards(curX, targetX);
    curY = snap ? targetY : SlideTowards(curY, targetY);
    *x = curX;
    *y = curY;
}

// The pause panel (sprite groups SG_PAUSE_*, used for nothing else on the
// boards) goes in the middle of the table like every other pause, also when
// the game pauses during a scene the GBA screen is drawn at the camera for
// (evolution, catch, hatch, Ball Saver): ppu.c then draws its sprites apart
// at PortPauseSpot, and the scene stays where it lines up with the board.
int PortPauseSprite(int index)
{
    return (gMain.modeChangeFlags & MODE_CHANGE_PAUSE) && index >= 0 && index < PORT_MAX_SPRITES && sOwner[index]
        && sOwner[index] >= &gMain.spriteGroups[SG_PAUSE_PANEL]
        && sOwner[index] <= &gMain.spriteGroups[SG_PAUSE_BOTTOM_BORDER];
}

// Board position of the GBA screen the pause panel is drawn with.
void PortPauseSpot(int *x, int *y)
{
    *x = POPUP_SPOT_X;
    *y = POPUP_SPOT_Y;
}

// The HUD (score row; mode timer) is drawn where it is on the GBA when the
// camera is at the bottom of the table, whatever the camera does. ppu.c pulls
// its pixels out of the GBA screen's BG0 by their place in the BG0 map (see
// PORT_HUD_* in jvppbport.h). Returns the board position of the top-left
// corner of that bottom GBA screen.
void PortHudSpot(int *x, int *y)
{
    *x = POPUP_SPOT_X;
    *y = gBoardConfig.fieldLayout.cameraScrollMaxY - 80;  // the camera's lowest top edge
}

// The timer's map cells hold the timer (and not, say, part of a panel).
int PortHudTimerShown(void)
{
    return gCurrentPinballGame->eventTimerType != EVENT_TIMER_MODE_NONE;
}

// For "hide it if it's below the screen" checks: table positions are never
// below the screen in the static view.
int PortScreenValue(int value)
{
    return !PortTableView() || value < WORLD_BIAS / 2;
}

static int OamIndex(struct OamData *oam)
{
    ptrdiff_t i = oam - gOamBuffer;
    return (i >= 0 && i < PORT_MAX_SPRITES) ? (int)i : -1;
}

static int FullX(struct OamData *oam)
{
    int i = OamIndex(oam);

    if (i >= 0 && sHasFullX[i] && (sFullX[i] & 0x1FF) == oam->x)
        return sFullX[i];
    return oam->x >= 256 ? oam->x - 512 : oam->x;
}

static int FullY(struct OamData *oam)
{
    int i = OamIndex(oam);

    if (i >= 0 && sHasFullY[i] && (sFullY[i] & 0xFF) == oam->y)
        return sFullY[i];
    return oam->y >= 128 ? oam->y - 256 : oam->y;
}

int PortOamSetX(struct OamData *oam, int value)
{
    int i = OamIndex(oam);

    oam->x = value;
    if (i >= 0)
    {
        sFullX[i] = value;
        sHasFullX[i] = TRUE;
        if (sOwner[i])
        {
            sRefX[i] = sOwner[i]->baseX;
            sHasRefX[i] = TRUE;
        }
    }
    return value;
}

int PortOamSetY(struct OamData *oam, int value)
{
    int i = OamIndex(oam);

    oam->y = value;
    if (i >= 0)
    {
        sFullY[i] = value;
        sHasFullY[i] = TRUE;
        if (sOwner[i])
        {
            sRefY[i] = sOwner[i]->baseY;
            sHasRefY[i] = TRUE;
        }
    }
    return value;
}

int PortOamAddX(struct OamData *oam, int value)
{
    return PortOamSetX(oam, FullX(oam) + value);
}

int PortOamAddY(struct OamData *oam, int value)
{
    return PortOamSetY(oam, FullY(oam) + value);
}

// LoadSpriteSets() puts the sprites of each active group into gOamBuffer.
void PortOamOwner(int index, struct SpriteGroup *group)
{
    if (index < PORT_MAX_SPRITES)
        sOwner[index] = group;
}

// The value congruent to `value` (mod `period`) that is nearest to `near`.
static int Nearest(int value, int near, int period)
{
    int diff = (value - near) % period;

    if (diff < -period / 2)
        diff += period;
    else if (diff >= period / 2)
        diff -= period;
    return near + diff;
}

// LoadSpriteSets() has just rebuilt gOamBuffer from the sprite templates.
void PortOamLoaded(int count)
{
    static int warned;

    if (count > PORT_MAX_SPRITES && !warned)
    {
        fprintf(stderr, "JVPPBPort: %d sprites, more than the %d the port has room for\n", count, PORT_MAX_SPRITES);
        warned = TRUE;
    }
    sLoadedCount = count;
    memset(sHasFullX, 0, sizeof(sHasFullX));
    memset(sHasFullY, 0, sizeof(sHasFullY));
    memset(sHasRefX, 0, sizeof(sHasRefX));
    memset(sHasRefY, 0, sizeof(sHasRefY));
    memset(sOwner + count, 0, (PORT_MAX_SPRITES - (count < PORT_MAX_SPRITES ? count : PORT_MAX_SPRITES)) * sizeof(sOwner[0]));
}

// ---- Board background rows ----

static const u8 *BufferRow(int row)
{
    return row < 32 ? &gBoardBGTileBuffer[row * ROW_BYTES] : &gBoardBGTileBufferAlt[(row - 32) * ROW_BYTES];
}

// The game has (re)decompressed tiles into its board buffers.
void PortBoardBuffersWritten(const void *dest, uint32_t size)
{
    const u8 *d = dest;

    if ((d < gBoardBGTileBuffer + 32 * ROW_BYTES && d + size > gBoardBGTileBuffer)
        || (d < gBoardBGTileBufferAlt + 24 * ROW_BYTES && d + size > gBoardBGTileBufferAlt))
    {
        memset(sRowCopied, 0, sizeof(sRowCopied));
        memset(sSlotRow, -1, sizeof(sSlotRow));
        memset(sLitSet, 0, sizeof(sLitSet));
    }
}

static u8 *RowTiles(int row)
{
    if (!sRowCopied[row])
    {
        memcpy(sRowTiles[row], BufferRow(row), ROW_BYTES);
        sRowCopied[row] = TRUE;
    }
    return sRowTiles[row];
}

// Which board row a write of lit-up graphics from `source` to this VRAM
// offset is meant for: by graphic and address if listed, else by address,
// else the row loaded in that slot.
static int WrapRow(const void *source, int vramOffset, int slot)
{
    int i, byAddress = -1;

    for (i = 0; i < gPortWrapRowCount; i++)
    {
        const struct PortWrapRow *entry = &gPortWrapRows[i];
        if (entry->offset != vramOffset || entry->field != gMain.selectedField)
            continue;
        if (entry->source == source)
            return entry->row;
        if (entry->source == NULL && (byAddress < 0 || entry->row == sSlotRow[slot]))
            byAddress = entry->row;
    }
    return byAddress >= 0 ? byAddress : sSlotRow[slot];
}

// A DMA has just written `size` bytes from `src` to `dest`.
void PortStreamedWrite(const void *src, const void *dest, uint32_t size)
{
    const u8 *base = &gPortVram[STREAMED_VRAM];
    const u8 *d = dest, *s = src;
    u32 done = 0;

    if (gMain.selectedField >= MAIN_FIELD_COUNT || d + size <= base || d >= base + BOARD_SLOTS * ROW_BYTES)
        return;
    while (done < size)
    {
        int offset = d + done - base;
        int slot = offset / ROW_BYTES, inRow = offset % ROW_BYTES;
        u32 chunk = ROW_BYTES - inRow;
        int row = -1;

        if (chunk > size - done)
            chunk = size - done;
        if (offset >= 0 && slot < BOARD_SLOTS)
        {
            const u8 *from = s + done;
            if (from >= gBoardBGTileBuffer && from < gBoardBGTileBuffer + 32 * ROW_BYTES)
                row = (from - gBoardBGTileBuffer) / ROW_BYTES;
            else if (from >= gBoardBGTileBufferAlt && from < gBoardBGTileBufferAlt + 24 * ROW_BYTES)
                row = 32 + (from - gBoardBGTileBufferAlt) / ROW_BYTES;
            if (row >= 0)
            {
                // A row load: the plain tiles, then the row's lights over them.
                u32 i;
                sSlotRow[slot] = row;
                if (row < BOARD_ROWS)
                {
                    u8 *tiles = RowTiles(row);
                    memcpy(tiles + inRow, d + done, chunk);
                    for (i = inRow; i < inRow + chunk; i++)
                    {
                        if (sLitSet[row][i])
                            tiles[i] = sLit[row][i];
                    }
                }
            }
            else
            {
                row = WrapRow(done == 0 ? src : NULL, offset, slot);  // lit-up graphics
                if (row >= 0 && row < BOARD_ROWS)
                {
                    memcpy(RowTiles(row) + inRow, d + done, chunk);
                    memcpy(&sLit[row][inRow], d + done, chunk);
                    memset(&sLitSet[row][inRow], TRUE, chunk);
                }
            }
        }
        done += chunk;
    }
}

// Tile data of a board tile (index `tile` in char block 2) on board row
// `mapRow`, or NULL if it isn't one of the streamed board rows.
const u8 *PortStreamedTile(int tile, int mapRow)
{
    if (mapRow >= BOARD_ROWS || (tile >> 5) != (mapRow + 10) % BOARD_SLOTS)
        return NULL;
    return (sRowCopied[mapRow] ? sRowTiles[mapRow] : BufferRow(mapRow)) + (tile & 31) * 32;
}

// Debug aid: JVPPBPORT_SPRITEDUMP=N prints the sprite list at the Nth copy
// made while a table is shown.
static void DumpSprites(void)
{
    static long dumpAt = -2, copies;
    int i;

    if (dumpAt == -2)
        dumpAt = getenv("JVPPBPORT_SPRITEDUMP") ? strtol(getenv("JVPPBPORT_SPRITEDUMP"), NULL, 0) : -1;
    if (!PortTableView() || ++copies != dumpAt)
        return;
    for (i = 0; i < gPortSpriteCount; i++)
    {
        const struct PortSprite *sp = &gPortSprites[i];
        static const char *const kinds[] = {"screen", "table", "hidden"};
        fprintf(stderr, "sprite %3d: %-6s x=%5d y=%5d oam=%04x %04x %04x\n", i, kinds[sp->kind],
                sp->x, sp->y, sp->attr[0], sp->attr[1], sp->attr[2]);
    }
}

// Palette slot 11 holds either the hatch egg/cave colors (camera high) or
// Spoink's (camera low): the game swaps them as the camera moves
// (UpdateSpoinkAnimation). With the whole table shown, both are needed at
// once, so table sprites in that slot get their own copy of the right one.
// Only while the slot holds one of the two, or one of them dimmed by the
// pause menu (not during fades and flashes).
static void DimLikePause(u16 *out, const u16 *pal)
{
    int j;

    for (j = 0; j < 16; j++)
        out[j] = RGB5(RGB5_GET_R(pal[j]) * 2 / 5, RGB5_GET_G(pal[j]) * 2 / 5, RGB5_GET_B(pal[j]) * 2 / 5);
}

static void FixSharedPalette(int i, struct PortSprite *sprite)
{
    const u16 *slot = (const u16 *)OBJ_PLTT + PAL_IX_SPOINK * 16;
    const u16 *egg = gFieldVariant_Pals[gMain.selectedField][gCurrentPinballGame->paletteDimmingIx * 2];
    const u16 *spoink = gFieldVariant_Pals[gMain.selectedField][gCurrentPinballGame->paletteDimmingIx * 2 + 1];
    struct SpriteGroup *spoinkGroup = &gMain.spriteGroups[gMain.selectedField == FIELD_RUBY
                                                          ? SG_RUBY_SPOINK_LAUNCHER : SG_SAPPHIRE_SPOINK_LAUNCHER];

    u16 eggDim[16], spoinkDim[16];
    int dimmed = FALSE;

    if ((sprite->attr[2] >> 12) != PAL_IX_SPOINK)
        return;
    if (memcmp(slot, egg, 32) != 0 && memcmp(slot, spoink, 32) != 0)
    {
        // Paused: PauseGame dims every sprite palette to 2/5.
        DimLikePause(eggDim, egg);
        DimLikePause(spoinkDim, spoink);
        if (memcmp(slot, eggDim, 32) != 0 && memcmp(slot, spoinkDim, 32) != 0)
            return;
        dimmed = TRUE;
    }
    if (sOwner[i] == spoinkGroup)
        memcpy(gPortSpritePalettes[i], dimmed ? spoinkDim : spoink, 32);
    else
        memcpy(gPortSpritePalettes[i], dimmed ? eggDim : egg, 32);
    sprite->ownPalette = TRUE;
}

// Called when the game copies gOamBuffer to OAM (DefaultMainCallback).
// A drained ball: the game parks it at the drain, just below what the GBA
// shows. Here it's drawn falling on until it leaves the table at the
// bottom edge (nothing is drawn below the table).
// Returns how far below its real place to draw it this frame.
#define DRAIN_START_SPEED 48  // 1/16 pixels per frame
#define DRAIN_GRAVITY 4       // 1/16 pixels per frame, per frame

static int DrainFall(void)
{
    static int fall, speed;  // 1/16 pixels

    if (gCurrentPinballGame->ballStates[0].positionQ0.y < gBoardConfig.fieldLayout.ballDrainY
        || (gMain.modeChangeFlags & MODE_CHANGE_BALL_SAVER))
    {
        fall = 0;
        speed = DRAIN_START_SPEED;
        return 0;
    }
    if (fall < PORT_CANVAS_HEIGHT * 16)
    {
        fall += speed;
        speed += DRAIN_GRAVITY;
    }
    return fall / 16;
}

void PortSnapshotOam(void)
{
    int table = PortTableView();
    int count = sLoadedCount > 128 ? sLoadedCount : 128;
    int drainFall = table ? DrainFall() : 0;
    int i;

    if (count > PORT_MAX_SPRITES)
        count = PORT_MAX_SPRITES;
    for (i = 0; i < count; i++)
    {
        struct OamData *oam = &gOamBuffer[i];
        struct PortSprite *sprite = &gPortSprites[i];
        int x = FullX(oam), y = FullY(oam);
        int onTableX = x >= WORLD_BIAS / 2, onTableY = y >= WORLD_BIAS / 2;

        memcpy(sprite->attr, oam, sizeof(sprite->attr));
        // The game adds piece offsets that it keeps as unsigned 9-/8-bit
        // numbers (LoadSpriteSets), so a piece left of or above its group's
        // origin comes out 512 or 256 too far. Pieces sit near their group.
        if (sOwner[i] && onTableX && onTableY)
        {
            x = Nearest(x, sHasRefX[i] ? sRefX[i] : sOwner[i]->baseX, 512);
            y = Nearest(y, sHasRefY[i] ? sRefY[i] : sOwner[i]->baseY, 256);
        }
        sprite->x = x - WORLD_BIAS;
        sprite->y = y - WORLD_BIAS;
        if (drainFall && sOwner[i] == gMain.fieldSpriteGroups[FIELD_SG_BALL])
            sprite->y += drainFall;
        sprite->ownPalette = FALSE;
        if (!table || (!onTableX && !onTableY))
            sprite->kind = PORT_SPRITE_SCREEN;
        else if (onTableX && onTableY)
        {
            sprite->kind = PORT_SPRITE_TABLE;
            FixSharedPalette(i, sprite);
        }
        else
            sprite->kind = PORT_SPRITE_HIDDEN;
    }
    gPortSpriteCount = count;
    DumpSprites();
}
