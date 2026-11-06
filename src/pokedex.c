#include "global.h"
#include "functions.h"
#include "link.h"
#include "m4a.h"
#include "main.h"
#include "types.h"
#include "variables.h"
#include "constants/bg_music.h"
#include "constants/characters.h"
#include "constants/global.h"
#include "constants/mem_layout/pokedex.h"

#define DEX_NUM_DIGITS      3
#define SCROLL_WAIT_FRAMES  9
#define ENTRIES_SHOWN_COUNT 5

#ifdef JVPPBPORT
// JVPPBPort's tall window shows as many list rows as fit (JVPPBPort/src/
// layouts.c; the list fills the screen): the
// game draws the first 5 as usual; PortCaptureSecondPage has it draw the
// next pages of 5 for a moment and keeps pictures of them. The cursor
// moves over all rows. With JVPPBPORT_GBAVIEW=1 it's 5 rows, as on the GBA.
#include <string.h>
#include "jvppbport.h"
#define LIST_ROWS (gPortStaticView ? PortDexListRows() : ENTRIES_SHOWN_COUNT)
static void PortCaptureSecondPage(int backdrop);
static void PortDexPlaceSprites(struct SpriteGroup *cursor, struct SpriteGroup *knob);
static void PortCaptureDrawer(void);
static void PortCaptureDrawerText(void);
// The description drawer shows all its text at once (no page turning).
#define ALL_TEXT_SHOWN gPortStaticView
#else
#define LIST_ROWS ENTRIES_SHOWN_COUNT
#define ALL_TEXT_SHOWN FALSE
#endif
// The list position that shows the last entry in the bottom row.
#define LAST_LIST_POSITION (gPokedexListEntryCount - LIST_ROWS)

#define DEX_TEXT_PAGES 2
#define DEX_TEXT_ROWS 3
#define DEX_TEXT_CHARS_PER_PAGE (DEX_TEXT_ROWS * POKEMON_DEX_LINE_LENGTH)

enum PokedexStates
{
    POKEDEX_STATE_LOAD_GRAPHICS,
    POKEDEX_STATE_HANDLE_LIST_INPUT,
    POKEDEX_STATE_2,
    POKEDEX_STATE_3,
    POKEDEX_STATE_4,
    POKEDEX_STATE_5,
    POKEDEX_STATE_LINK_SETUP,
    POKEDEX_STATE_7,
    POKEDEX_STATE_8,
    POKEDEX_STATE_9,
    POKEDEX_STATE_10,
    POKEDEX_STATE_DELETE_CONFIRMATION,
    POKEDEX_STATE_RETURN_TO_TITLE,
};

#define SHOW_IX_CATCH_SPRITE 0
#define SHOW_IX_HATCH_SPRITE 1

void RefreshPokedexListDisplay(void);
static void PokedexListScrollUp(void);
static void PokedexListScrollDown(void);
static void PokedexListScrollUpFast(void);
static void PokedexListScrollDownFast(void);
void Pokedex_CheckDeleteKeyComboPressed(void);
void UpdateMonSpriteVisibility(void);
void RefreshMonPreviewSprite(void);
static s16 Pokedex_ProcessLinkExchange(void);
s16 SendLinkPokedexData(void);
void PrintDexDescription(s16, u32);
void LoadPokedexFlagsFromSave(void);
void RenderPokedexSprites(void);
static void RenderLinkGraphics(void);
static void InitLinkTransferState(void);
int MasterReceivePokedexFlags(void);
static int MasterSendPokedexFlags(void);
static int ClientSendPokedexFlags(void);
static int ClientReceivePokedexFlags(void);
static void PrintSelectedMonDexNum(s16);
static void PrintSeenOwnedTotals(s16, s16);
void BlitGlyphToTileBuffer(int, int, int);
void PrintDexNumbersFromListPosition(s16);
static void PrintCaughtBallFromListPosition(s16);
void LoadMonPortrait(s16);

EWRAM_DATA s8 gPokedex_EraseSaveDataAccessCounter;
EWRAM_DATA s8 gPokedex_EraseSaveDataAccessStep;
EWRAM_DATA s8 gPokedexScrollActive;
EWRAM_DATA s8 gPokedexSpriteIndexBase;
EWRAM_DATA u16 gPokedexVramBuffer[0x400];
EWRAM_DATA s16 gPokedexSpriteAnimTimer;
EWRAM_DATA s16 gLinkTimeoutCounter;
EWRAM_DATA s16 gPokedexAnimatedIconFrame;
EWRAM_DATA u16 gLinkRecvBuffer[0x8][2];
EWRAM_DATA s16 gPokedexCursorBlinkOffset;
EWRAM_DATA u16 gLinkExchangeFrameCounter;
EWRAM_DATA s16 gPokedexNumOwned;
EWRAM_DATA s16 gPokedexLinkStateTimer;
EWRAM_DATA u8 gLinkExchangeStep;
EWRAM_DATA s8 gPokedexLinkTransferPhase;
EWRAM_DATA u16 gGlyphLowerRowBuffer[0x20];
EWRAM_DATA u16 gPokedexLinkSendCounter;
EWRAM_DATA u8 gLinkPlayerCount;
EWRAM_DATA s8 gPokedexShowPageIndicator;
EWRAM_DATA s8 gPokedexMasterHandshakeState;
EWRAM_DATA s16 gPokedexFlags[232];
EWRAM_DATA s16 gPokedexFlagExchangeBuffer[226];
EWRAM_DATA s16 gPokedexInfoWindowSlideStep;
EWRAM_DATA s16 gPokedexShowPortrait;
EWRAM_DATA s16 gPokedexShowCatchHatch[2];
EWRAM_DATA s16 gPokedexListEntryCount;
EWRAM_DATA s16 gPokedexCursorOffset;
EWRAM_DATA s16 gPokedexShowAnimSprite;
EWRAM_DATA u16 gPokedexInfoWindowBackupTiles[0x400];
EWRAM_DATA u32 gLinkStatusResult;
EWRAM_DATA u8 gLinkNegotiationFlags;
EWRAM_DATA s16 gPokedexSelectedMon;
EWRAM_DATA u32 gLinkConnectionState;
EWRAM_DATA s16 gPokedexBlinkTimer;
EWRAM_DATA u16 gGlyphUpperRowBuffer[0x20];
EWRAM_DATA s16 gPokedexNumSeen;
EWRAM_DATA s8 gPokedexShowPopupWindow;
EWRAM_DATA u8 gLinkAdvanceState;
EWRAM_DATA s8 gPokedexHandshakeRetryCount;
EWRAM_DATA s8 gPokedexButtonPromptFrame;
EWRAM_DATA s16 gPokedexPageIndicatorTimer;
/****
 *  Yellow confirmation/info window mode
 *  0= Transmession connection prompt,
 *  1= Transferring in progress?
 *  2= transmission error message
 *  3= transfer complete?
 *  4= delete save data confirmation
 * ****/
EWRAM_DATA s8 gPokedexPopupTypeIndex;
EWRAM_DATA s16 gPokedexAnimatedIconTimer;
EWRAM_DATA s8 gPokedexShowButtonPrompt;
EWRAM_DATA u16 gPokedexScrollbarY;
EWRAM_DATA s16 gPokedexSpriteAnimFrame;
EWRAM_DATA s16 gPokedexSpriteCategory;
EWRAM_DATA s8 gPokedexClientHandshakeState;
EWRAM_DATA s16 gPokedexScrollWaitFrames;
EWRAM_DATA s8 gPokedexShowCompletionBadge;
EWRAM_DATA s8 gPokedexPageIndicatorBlink;
EWRAM_DATA s16 gPokedexListPosition;
EWRAM_DATA u16 gPokedexLinkChunkIndex;
EWRAM_DATA s16 gPokedexDetailFrameCount;
EWRAM_DATA s16 gLinkSendBuffer[0xA];
EWRAM_DATA s8 gPokedexDescriptionPage;

struct PokedexEntry
{
    /*0x00*/ u16 dexNum[DEX_NUM_DIGITS];
    /*0x06*/ u16 heightWeight[POKEMON_HEIGHT_WEIGHT_TEXT_LENGTH];
    /*0x18*/ s16 name[POKEMON_NAME_LENGTH];
    /*0x2C*/ s16 category[POKEMON_CATEGORY_NAME_LENGTH];
    // The dex description has 2 pages with 3 lines each. Each line
    // is 42 characters long.
    /*0x42*/ s16 description[2 * 3 * POKEMON_DEX_LINE_LENGTH];
}; /* size=0x23C */

// Rom_1 data
extern const u16 gDexInfoWindowMiddleRowTiles[];
extern const u16 gDexInfoWindowBottomRowTiles[];
extern const u16 gDexInfoWindowEmptyRowTiles[];
extern const u16 gDexInfoWindowEmptyTextRowTiles[];
extern const StateFunc gPokedexStateFuncs[];
extern const struct PokedexEntry gPokedexEntries[];
extern const u16 gPokedexBg1_Tilemap[];
extern const u16 gPokedexBgText_Gfx[];
extern const u16 gPokedexInfoWindowTilemap[];
extern const u16 gPokedexBg2_Tilemap[];
extern const u16 gPokedexBg_Gfx[];
extern const u16 gPokedexBg3_Tilemap[];
extern const Palette gPokedexBackground_Pals[];
extern const Palette gPokedexSprites_Pals[];
extern const u8 gPokedexSprites_Gfx[];
extern const u8 gPokedexTextGlyphs_Gfx[];

// Rom_2 data
extern const u16 gPokedexAnimBaseTileNums[][4];
extern const u16 gPokedexCatchAnimTileOffsets[][20];
extern const u16 gPokedexHatchAnimTileOffsets[][51];
extern const s16 gPokedexAnimFrameDurations[][51];
extern const struct SpriteSet *const gPokedexSpriteSets[];
extern const s16 gDexAnimationIx[];
extern const s16 gPokedexCatchAnimIndices[];
extern s16 gPokedexListNameVramOffsets[]; //TODO: should be const
extern const Palette *gCatchMonPaletteGroups[];
extern const Palette *gMonPortraitGroupPals[];
extern const u8 *gCatchSpriteGfxPtrs[SIZE_OF_VRAM_CATCH_MON_ENTITY_TILES];
extern const u8 *gMonPortraitGroupGfx[];

enum PokedexPopupType {
    POKEDEX_POPUP_TRANSMISSION_CONNECT_PROMPT = 0,
    POKEDEX_POPUP_TRANSMITTING_ACTIVE = 1,
    POKEDEX_POPUP_TRANSMISSION_ERROR = 2,
    POKEDEX_POPUP_TRANSMISSION_COMPLETE = 3,
    POKEDEX_POPUP_DELETE_CONFIRMATION_PROMPT = 4
};

// The japanese and english text glyphs are sourced from the same blob of tile graphics, and
// each glyph is two tiles high.
#define ENGLISH_GLYPHS_START 0x2820



void PokedexMain(void)
{
    gPokedexStateFuncs[gMain.subState]();
}

void LoadPokedexGraphics(void)
{
    ResetDisplayState();

    REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_OBJ_1D_MAP | DISPCNT_FORCED_BLANK | DISPCNT_OBJ_ON;
    REG_BG1CNT = BGCNT_PRIORITY(1) | BGCNT_CHARBASE(1) | BGCNT_16COLOR | BGCNT_SCREENBASE(0) | BGCNT_TXT256x256;
    REG_DISPCNT |= DISPCNT_BG1_ON;
    REG_BG2CNT = BGCNT_PRIORITY(2) | BGCNT_CHARBASE(3) | BGCNT_16COLOR | BGCNT_SCREENBASE(1) | BGCNT_TXT256x256;
    REG_DISPCNT |= DISPCNT_BG2_ON;
    REG_BG3CNT = BGCNT_PRIORITY(3) | BGCNT_CHARBASE(3) | BGCNT_16COLOR | BGCNT_SCREENBASE(2) | BGCNT_TXT256x256;
    REG_DISPCNT |= DISPCNT_BG3_ON;

    gMain.dispcntBackup = REG_DISPCNT;

    DmaCopy16(3, gPokedexBgText_Gfx, BG_VRAM_ADDR_POKEDEX_TEXT_TILES, SIZE_OF_VRAM_POKEDEX_TEXT_TILES);
    DmaCopy16(3, gPokedexBg_Gfx, BG_VRAM_ADDR_POKEDEX_FRAME_TILES, SIZE_OF_VRAM_POKEDEX_FRAME_TILES);
    DmaCopy16(3, gPokedexBackground_Pals, BG_PLTT, BG_PLTT_SIZE);
    DmaCopy16(3, gPokedexBg1_Tilemap, gBG0TilemapBuffer, MEM_SIZE_OF_TILEMAP_256_BY_256);
    DmaCopy16(3, gPokedexBg2_Tilemap, gPokedexVramBuffer, MEM_SIZE_OF_TILEMAP_256_BY_256);
    DmaCopy16(3, gPokedexBg3_Tilemap, BG_VRAM_ADDR_POKEDEX_BG3_TILEMAP, MEM_SIZE_OF_TILEMAP_256_BY_256);
    DmaCopy16(3, gPokedexSprites_Pals, OBJ_PLTT, OBJ_PLTT_SIZE);
    DmaCopy16(3, gPokedexSprites_Gfx, OBJ_VRAM_ADDR_POKEDEX_SPRITE_TILES, SIZE_OF_VRAM_POKEDEX_SPRITE_TILES);

    InitPokedexState();
    PrintSeenOwnedTotals(gPokedexNumSeen, gPokedexNumOwned);
    PrintSelectedMonDexNum(gPokedexSelectedMon);
    PrintDexNumbersFromListPosition(gPokedexListPosition);
    PrintCaughtBallFromListPosition(gPokedexListPosition);
    LoadMonPortrait(gPokedexSelectedMon);
    LoadMonAnimationSprite(gPokedexSelectedMon);
    CheckMonHasAnimation(gPokedexSelectedMon);

    gPokedexVramBuffer[0x134] = 0x59;

    DmaCopy16(3, gBG0TilemapBuffer, BG_VRAM_ADDR_POKEDEX_BG1_TILEMAP, MEM_SIZE_OF_TILEMAP_256_BY_256);
    DmaCopy16(3, gPokedexVramBuffer,BG_VRAM_ADDR_POKEDEX_BG2_TILEMAP, MEM_SIZE_OF_TILEMAP_256_BY_256);
#ifdef JVPPBPORT
    PortCaptureSecondPage(TRUE);
#endif

    RenderPokedexSprites();
    EnableVBlankInterrupts();
    FadeInScreen();

    gMain.subState = POKEDEX_STATE_HANDLE_LIST_INPUT;

    m4aSongNumStart(MUS_POKEDEX);
}

void InitPokedexState(void)
{
    s32 i;

    gPokedexSelectedMon = 0;
    gPokedexListPosition = 0;
    gPokedexAnimatedIconFrame = 0;
    gPokedexAnimatedIconTimer = 0;
    gPokedexCursorOffset = 0;
    gPokedexCursorBlinkOffset = 0;
    gPokedexBlinkTimer = 0;
    gPokedexScrollWaitFrames = 0;
    gPokedexScrollActive = FALSE;
    gPokedexSpriteAnimFrame = 0;
    gPokedexSpriteAnimTimer = 0;
    gPokedexDetailFrameCount = 0;
    gPokedexPageIndicatorTimer = 0;
    gPokedexShowAnimSprite = TRUE;
    gPokedexShowPortrait = TRUE;

    gPokedexShowCatchHatch[SHOW_IX_CATCH_SPRITE] = FALSE;
    gPokedexShowCatchHatch[SHOW_IX_HATCH_SPRITE] = FALSE;

    gPokedexSpriteCategory = 0;
    gPokedexInfoWindowSlideStep = 0;
    gPokedexButtonPromptFrame = 0;
    gPokedexShowButtonPrompt = TRUE;

    LoadPokedexFlagsFromSave();
    if (CheckAllPokemonCaught() == TRUE)
    {
        gPokedexShowCompletionBadge = TRUE;
    }
    else
    {
        gPokedexShowCompletionBadge = FALSE;
    }

    gPokedexShowPopupWindow = FALSE;
    gPokedexPopupTypeIndex = POKEDEX_POPUP_TRANSMISSION_CONNECT_PROMPT;
    gPokedexLinkStateTimer = 0;
    gPokedex_EraseSaveDataAccessCounter = 0;
    gPokedex_EraseSaveDataAccessStep = 0;
    gPokedexDescriptionPage = 0;
    gPokedexShowPageIndicator = 0;
    gPokedexPageIndicatorBlink = 0;
    gPokedexSpriteIndexBase = 0;

    for (i = 0; i < 0xE1; i++)
    {
        gPokedexFlagExchangeBuffer[i] = gPokedexFlags[i];
    }

    if (gPokedexFlags[SPECIES_AERODACTYL])
        gPokedexListEntryCount = SPECIES_AERODACTYL + 1;
    else if (gPokedexFlags[SPECIES_TOTODILE])
        gPokedexListEntryCount = SPECIES_TOTODILE + 1;
    else if (gPokedexFlags[SPECIES_CYNDAQUIL])
        gPokedexListEntryCount = SPECIES_CYNDAQUIL + 1;
    else if (gPokedexFlags[SPECIES_CHIKORITA])
        gPokedexListEntryCount = SPECIES_CHIKORITA + 1;
    else if (gPokedexFlags[SPECIES_JIRACHI])
        gPokedexListEntryCount = SPECIES_JIRACHI + 1;
    else
        gPokedexListEntryCount = SPECIES_RAYQUAZA + 1;
}

void Pokedex_HandleListInput(void)
{
    if (JOY_HELD(SELECT_BUTTON))
    {
        gPokedexShowButtonPrompt = FALSE;

        if (JOY_HELD(DPAD_UP))
        {
            PokedexListScrollUp();
            UpdateMonSpriteVisibility();
            RefreshMonPreviewSprite();
            gMain.subState = POKEDEX_STATE_2;
        }
        else if (JOY_HELD(DPAD_DOWN))
        {
            PokedexListScrollDown();
            UpdateMonSpriteVisibility();
            RefreshMonPreviewSprite();
            gMain.subState = POKEDEX_STATE_2;
        }
        else if (JOY_HELD(DPAD_LEFT))
        {
            PokedexListScrollUpFast();
            UpdateMonSpriteVisibility();
            RefreshMonPreviewSprite();
            gMain.subState = POKEDEX_STATE_2;
        }
        else if (JOY_HELD(DPAD_RIGHT))
        {
            PokedexListScrollDownFast();
            UpdateMonSpriteVisibility();
            RefreshMonPreviewSprite();
            gMain.subState = POKEDEX_STATE_2;
        }
        else
        {
            gPokedexScrollActive = FALSE;
            UpdateMonSpriteVisibility();
        }

        if (JOY_NEW(A_BUTTON))
        {
            gPokedexDetailFrameCount = 0;

            if (gPokedexFlags[gPokedexSelectedMon] > SPECIES_DEX_SEEN)
            {
                //Store the tilemap data for the space behind the expanded drawer that shows the dex text.
                gPokedexShowButtonPrompt = FALSE;
                DmaCopy16(3, BG_VRAM_ADDR_POKEDEX_DEX_TEXT_TILEMAP, gPokedexInfoWindowBackupTiles, SIZE_OF_VRAM_POKEDEX_DEX_TEXT_PLUS_HANDLE_TILEMAP);
#ifdef JVPPBPORT
                PortCaptureDrawer();
#endif
                gMain.subState = POKEDEX_STATE_3;
            }
            else
            {
                m4aSongNumStart(SE_FAILURE);
            }
        }
    }
    else
    {
        gPokedexShowButtonPrompt = TRUE;

        if (JOY_HELD(DPAD_UP))
        {
            PokedexListScrollUp();
            RefreshPokedexListDisplay();
        }
        else if (JOY_HELD(DPAD_DOWN))
        {
            PokedexListScrollDown();
            RefreshPokedexListDisplay();
        }
        else if (JOY_HELD(DPAD_LEFT))
        {
            PokedexListScrollUpFast();
            RefreshPokedexListDisplay();
        }
        else if (JOY_HELD(DPAD_RIGHT))
        {
            PokedexListScrollDownFast();
            RefreshPokedexListDisplay();
        }
        else
        {
            gPokedexScrollActive = FALSE;
        }

        if (CheckMonHasAnimation(gPokedexSelectedMon) == TRUE)
            gPokedexShowAnimSprite = TRUE;
        else
            gPokedexShowAnimSprite = FALSE;

        gPokedexShowPortrait = TRUE;
        gPokedexShowCatchHatch[SHOW_IX_CATCH_SPRITE] = FALSE;
        gPokedexShowCatchHatch[SHOW_IX_HATCH_SPRITE] = FALSE;
        gPokedexSpriteAnimTimer = 0;
        gPokedexSpriteAnimFrame = 0;

        if (JOY_NEW(A_BUTTON))
        {
            gPokedexDetailFrameCount = 0;

            if (gPokedexFlags[gPokedexSelectedMon] >= SPECIES_DEX_SHARED)
            {
                gPokedexShowButtonPrompt = FALSE;
                DmaCopy16(3, BG_VRAM_ADDR_POKEDEX_DEX_TEXT_TILEMAP, gPokedexInfoWindowBackupTiles, SIZE_OF_VRAM_POKEDEX_DEX_TEXT_PLUS_HANDLE_TILEMAP);
#ifdef JVPPBPORT
                PortCaptureDrawer();
#endif
                gMain.subState = POKEDEX_STATE_3;
            }
            else
            {
                m4aSongNumStart(SE_FAILURE);
            }
        }
        else if (JOY_NEW(B_BUTTON))
        {
            m4aSongNumStart(SE_MENU_CANCEL);
            gMain.subState = POKEDEX_STATE_RETURN_TO_TITLE;
        }
#ifndef JVPPBPORT
        // The port has no link cable: no transmission prompt.
        else if (JOY_NEW(START_BUTTON))
        {
            m4aSongNumStart(SE_MENU_POPUP_OPEN);
            gPokedexShowPopupWindow = TRUE;
            gPokedexPopupTypeIndex = POKEDEX_POPUP_TRANSMISSION_CONNECT_PROMPT;
            gPokedexShowButtonPrompt = FALSE;
            gPokedexShowAnimSprite = FALSE;
            gMain.subState = POKEDEX_STATE_LINK_SETUP;
        }
#endif

        Pokedex_CheckDeleteKeyComboPressed();
    }

    if (gPokedexScrollWaitFrames > 0)
        gPokedexScrollWaitFrames--;

    RenderPokedexSprites();
    DmaCopy16(3, gBG0TilemapBuffer, BG_VRAM_ADDR_POKEDEX_BG1_TILEMAP, MEM_SIZE_OF_TILEMAP_256_BY_256);
}

void Pokedex_PostScrollRefresh(void)
{
    RefreshPokedexListDisplay();
    gPokedexShowAnimSprite = FALSE;

    if (gPokedexScrollWaitFrames > 0)
        gPokedexScrollWaitFrames--;

    RenderPokedexSprites();
    DmaCopy16(3, gBG0TilemapBuffer, BG_VRAM_ADDR_POKEDEX_BG1_TILEMAP, MEM_SIZE_OF_TILEMAP_256_BY_256);
    gMain.subState = POKEDEX_STATE_HANDLE_LIST_INPUT;
}

void Pokedex_InfoWindowSlideIn(void)
{
    int i;

    for (i = 0; i < 0x20; i++)
    {
        gBG0TilemapBuffer[0x20*(gPokedexInfoWindowSlideStep + 10) + i] = gDexInfoWindowEmptyTextRowTiles[i];
        gPokedexVramBuffer[0x20*(gPokedexInfoWindowSlideStep + 9) + i] = gDexInfoWindowMiddleRowTiles[i];
        gPokedexVramBuffer[0x20*(gPokedexInfoWindowSlideStep + 10) + i] = gDexInfoWindowBottomRowTiles[i];
    }

    gPokedexInfoWindowSlideStep++;
    gPokedexVramBuffer[0x134] = 0x59;
    DmaCopy16(3, gBG0TilemapBuffer, BG_VRAM_ADDR_POKEDEX_BG1_TILEMAP, MEM_SIZE_OF_TILEMAP_256_BY_256);
    DmaCopy16(3, gPokedexVramBuffer, BG_VRAM_ADDR_POKEDEX_BG2_TILEMAP, MEM_SIZE_OF_TILEMAP_256_BY_256);

    if (gPokedexInfoWindowSlideStep > 7)
    {
        gPokedexInfoWindowSlideStep = 0;
        gPokedexDetailFrameCount = 0;
        gPokedexPageIndicatorTimer = 0;
        gPokedexSpriteIndexBase = 0;
        gPokedexPageIndicatorBlink = 0;

        if (gPokedexSelectedMon < BONUS_SPECIES_START && !ALL_TEXT_SHOWN)
            gPokedexShowPageIndicator = 1;

        DmaCopy16(3, gPokedexInfoWindowTilemap, BG_VRAM_ADDR_POKEDEX_DEX_TEXT_TILEMAP, SIZE_OF_VRAM_POKEDEX_DEX_TEXT_TILEMAP);
        PrintDexDescription(gPokedexSelectedMon, gPokedexDescriptionPage);
#ifdef JVPPBPORT
        PortCaptureDrawerText();
#endif
        m4aMPlayVolumeControl(&gMPlayInfo_BGM, TRACKS_ALL, 0x40);
        PlayCry_NormalNoDucking(gSpeciesInfo[gPokedexSelectedMon].speciesIdRS, 0, 127, 10);
        gMain.subState = POKEDEX_STATE_5;
    }

}

void Pokedex_DetailViewInput(void)
{

    if (gPokedexDetailFrameCount < 0x51)
    {
        gPokedexDetailFrameCount++;
        if (gPokedexDetailFrameCount == 0x50)
        {
            m4aMPlayVolumeControl(&gMPlayInfo_BGM, TRACKS_ALL, 0x100);
        }
    }
    gPokedexPageIndicatorTimer++;
    if (0x1e < gPokedexPageIndicatorTimer)
    {
        gPokedexPageIndicatorTimer = 0;
        gPokedexPageIndicatorBlink = 1 - gPokedexPageIndicatorBlink;
    }
    gPokedexBlinkTimer = 0;

    if (JOY_NEW(DPAD_UP))
    {
        if ((gPokedexSelectedMon < BONUS_SPECIES_START) && (gPokedexDescriptionPage == 1) && !ALL_TEXT_SHOWN)
        {
            m4aSongNumStart(SE_DEX_INFO_FIELD_SELECT_MOVE);
            gPokedexDescriptionPage = 0;
            PrintDexDescription(gPokedexSelectedMon, 0);
            gPokedexSpriteIndexBase = gPokedexDescriptionPage;
        }
    }
    else if (JOY_NEW(DPAD_DOWN))
    {
        if ((gPokedexSelectedMon < BONUS_SPECIES_START) && (gPokedexDescriptionPage == 0) && !ALL_TEXT_SHOWN)
        {
            m4aSongNumStart(SE_DEX_INFO_FIELD_SELECT_MOVE);
            gPokedexDescriptionPage = 1;
            PrintDexDescription(gPokedexSelectedMon, 1);
            gPokedexSpriteIndexBase = gPokedexDescriptionPage;
        }
    }

    if (JOY_NEW(A_BUTTON))
    {
        if (gPokedexSelectedMon < BONUS_SPECIES_START)
        {
            if (!gPokedexDescriptionPage && !ALL_TEXT_SHOWN)
            {
                m4aSongNumStart(SE_DEX_INFO_FIELD_SELECT_MOVE);
                gPokedexDescriptionPage = 1 - gPokedexDescriptionPage;
                PrintDexDescription(gPokedexSelectedMon, gPokedexDescriptionPage);
                gPokedexSpriteIndexBase = gPokedexDescriptionPage;
            }
            else
            {
                m4aSongNumStart(SE_MENU_POPUP_CLOSE);
                m4aMPlayVolumeControl(&gMPlayInfo_BGM, TRACKS_ALL, 0x100);
                gPokedexDescriptionPage = 0;
                gPokedexDetailFrameCount = 0;
                gPokedexPageIndicatorTimer = gPokedexDetailFrameCount;
                gPokedexShowPageIndicator = 0;
                gMain.subState = POKEDEX_STATE_4;
            }
        }
    }
    else if (JOY_NEW(B_BUTTON))
    {
        m4aSongNumStart(SE_MENU_POPUP_CLOSE);
        m4aMPlayVolumeControl(&gMPlayInfo_BGM, TRACKS_ALL, 0x100);
        gPokedexDescriptionPage = 0;
        gPokedexDetailFrameCount = 0;
        gPokedexPageIndicatorTimer = gPokedexDetailFrameCount;
        gPokedexShowPageIndicator = 0;
        gMain.subState = POKEDEX_STATE_4;
    }

    if (JOY_HELD(SELECT_BUTTON))
    {
        if (gPokedexFlags[gPokedexSelectedMon] == SPECIES_DEX_CAUGHT)
        {
            if (gDexAnimationIx[gPokedexSelectedMon] == -1)
            {
                gPokedexShowAnimSprite = FALSE;
                gPokedexShowPortrait = TRUE;
                gPokedexShowCatchHatch[SHOW_IX_CATCH_SPRITE] = FALSE;
                gPokedexShowCatchHatch[SHOW_IX_HATCH_SPRITE] = FALSE;
                gPokedexSpriteAnimTimer = 0;
                gPokedexSpriteAnimFrame = 0;
            }
            else
            {
                if (gDexAnimationIx[gPokedexSelectedMon] < 100)
                {
                    gPokedexShowAnimSprite = FALSE;
                    gPokedexShowPortrait = FALSE;
                    gPokedexShowCatchHatch[SHOW_IX_CATCH_SPRITE] = TRUE;
                    gPokedexShowCatchHatch[SHOW_IX_HATCH_SPRITE] = FALSE;
                }
                else
                {
                    gPokedexShowAnimSprite = FALSE;
                    gPokedexShowPortrait = FALSE;
                    gPokedexShowCatchHatch[SHOW_IX_CATCH_SPRITE] = FALSE;
                    gPokedexShowCatchHatch[SHOW_IX_HATCH_SPRITE] = TRUE;
                }
            }
        }
        else
        {
            gPokedexShowAnimSprite = FALSE;
            gPokedexShowPortrait = TRUE;
            gPokedexShowCatchHatch[SHOW_IX_CATCH_SPRITE] = FALSE;
            gPokedexShowCatchHatch[SHOW_IX_HATCH_SPRITE] = FALSE;
        }
    }
    else
    {
        if (CheckMonHasAnimation(gPokedexSelectedMon) == TRUE)
            gPokedexShowAnimSprite = TRUE;
        else
            gPokedexShowAnimSprite = FALSE;

        gPokedexShowPortrait = TRUE;
        gPokedexShowCatchHatch[SHOW_IX_CATCH_SPRITE] = FALSE;
        gPokedexShowCatchHatch[SHOW_IX_HATCH_SPRITE] = FALSE;
        gPokedexSpriteAnimTimer = 0;
        gPokedexSpriteAnimFrame = 0;
    }

    RenderPokedexSprites();
}

void Pokedex_InfoWindowSlideOut(void)
{
    s32 i;

    for (i = 0; i < 0x20; i++)
    {
        gPokedexVramBuffer[0x20 * (0x11 - gPokedexInfoWindowSlideStep) + i] = gDexInfoWindowBottomRowTiles[i];
        gPokedexVramBuffer[0x20 * (0x12 - gPokedexInfoWindowSlideStep) + i] = gDexInfoWindowEmptyRowTiles[i];
    }

    if (gPokedexInfoWindowSlideStep < 8)
    {
        for (i = 0; i < 0x20; i++)
        {
            gBG0TilemapBuffer[0x20 * (0x11 - gPokedexInfoWindowSlideStep) + i] = gPokedexInfoWindowBackupTiles[0x20 * (0x7 - gPokedexInfoWindowSlideStep) + i];
        }
    }
    gPokedexInfoWindowSlideStep++;

    gPokedexVramBuffer[0x134] = 0x59;
    DmaCopy16(3, gPokedexVramBuffer, BG_VRAM_ADDR_POKEDEX_BG2_TILEMAP, MEM_SIZE_OF_TILEMAP_256_BY_256);
    DmaCopy16(3, gBG0TilemapBuffer, BG_VRAM_ADDR_POKEDEX_BG1_TILEMAP, MEM_SIZE_OF_TILEMAP_256_BY_256);

    if (gPokedexInfoWindowSlideStep > 8)
    {
        gPokedexInfoWindowSlideStep = 0;
        gPokedexShowAnimSprite = FALSE;
        gPokedexShowButtonPrompt = TRUE;

        DmaFill16(3, 0, gTempGfxBuffer, SIZE_OF_VRAM_POKEDEX_ENTRY_TEXT);
        DmaFill16(3, 0, BG_VRAM_ADDR_POKEDEX_ENTRY_TEXT, SIZE_OF_VRAM_POKEDEX_ENTRY_TEXT);
        gMain.subState = POKEDEX_STATE_HANDLE_LIST_INPUT;
    }
}

void Pokedex_LinkSetup(void)
{
    InitLinkHardware();
    InitLinkTransferState();
    gLinkExchangeStep = 0;
    RenderLinkGraphics();
    gMain.subState = POKEDEX_STATE_7;
}

void Pokedex_LinkTransferLoop(void)
{
    s16 var0;

    RenderLinkGraphics();

    if (JOY_NEW(B_BUTTON))
    {
        m4aSongNumStart(SE_MENU_CANCEL);
        gPokedexShowPopupWindow = FALSE;
        gPokedexPopupTypeIndex = POKEDEX_POPUP_TRANSMISSION_CONNECT_PROMPT;
        gPokedexShowButtonPrompt = TRUE;
        gPokedexShowAnimSprite = TRUE;
        ResetSerialAndInterrupts();
        gMain.subState = POKEDEX_STATE_HANDLE_LIST_INPUT;
    }
    else
    {
        gLinkStatusResult = LinkMain1(&gLinkAdvanceState, gLinkSendBuffer, gLinkRecvBuffer);
        gLinkConnectionState = gLinkStatusResult & 3;
        gLinkPlayerCount = (gLinkStatusResult & 0x1C) >> 2;
        gLinkNegotiationFlags = (gLinkStatusResult & 0xe00) >> 9;

        if ((gLinkStatusResult & 0x40) && (gLinkConnectionState < 2))
        {
            SendLinkPokedexData();

            if (!(gLinkStatusResult & 0x100))
            {
                var0 = Pokedex_ProcessLinkExchange();
                if (var0 == -1)
                {
                    gMain.subState = POKEDEX_STATE_9;
                }
                else if (var0 == 1)
                {
                    gPokedexPopupTypeIndex = POKEDEX_POPUP_TRANSMISSION_ERROR;
                    gMain.subState = POKEDEX_STATE_8;
                    m4aSongNumStart(SE_FAILURE);
                }
            }

            gLinkExchangeFrameCounter++;

            if ((gLinkStatusResult & 0x7f0000) && gPokedexLinkTransferPhase == -1)
            {
                gLinkTimeoutCounter++;
                if (0xB4 < gLinkTimeoutCounter)
                {
                    gPokedexPopupTypeIndex = POKEDEX_POPUP_TRANSMISSION_ERROR;
                    gMain.subState = POKEDEX_STATE_8;
                    m4aSongNumStart(SE_FAILURE);
                }
            }
        }
    }
}

void Pokedex_LinkRetryDelay(void)
{
    gLinkTimeoutCounter++;

    if (2 < gLinkTimeoutCounter) {
        gLinkTimeoutCounter = 0;
        gMain.subState = POKEDEX_STATE_LINK_SETUP;
    }
}

void Pokedex_LinkErrorTimeout(void)
{
    s32 iVar1;

    RenderLinkGraphics();
    gPokedexLinkStateTimer++;

    if (0x5A < gPokedexLinkStateTimer)
    {
        gPokedexLinkStateTimer = 0;
        gPokedexShowPopupWindow = FALSE;
        gPokedexPopupTypeIndex = POKEDEX_POPUP_TRANSMISSION_CONNECT_PROMPT;
        gPokedexShowButtonPrompt = TRUE;
        gPokedexShowAnimSprite = TRUE;

        ResetSerialAndInterrupts();
        DisableSerial();

        for(iVar1 = 0; iVar1 < 0xE1; iVar1++)
        {
            gPokedexFlagExchangeBuffer[iVar1] = gPokedexFlags[iVar1];
        }

        FadeOutScreen();
        m4aMPlayAllStop();
        DisableVBlankInterrupts();
        gMain.subState = POKEDEX_STATE_LOAD_GRAPHICS;
    }
}

void Pokedex_LinkSuccessSequence(void)
{
    s32 index;

    RenderLinkGraphics();
    switch (gPokedexLinkStateTimer)
    {
        case 0x4:
            ResetSerialAndInterrupts();
            DisableSerial();
            break;
        case 0x82:
            gPokedexPopupTypeIndex = POKEDEX_POPUP_TRANSMISSION_COMPLETE;
            m4aSongNumStart(SE_MENU_SELECT);
            break;
        case 0xFA:
            gPokedexLinkStateTimer = 0;
            gPokedexShowPopupWindow = FALSE;
            gPokedexPopupTypeIndex = POKEDEX_POPUP_TRANSMISSION_CONNECT_PROMPT;
            gPokedexShowButtonPrompt = TRUE;
            gPokedexShowAnimSprite = TRUE;
            for(index = 0; index < 0xE1; index++)
            {
                gPokedexFlags[index] = gPokedexFlagExchangeBuffer[index];
            }
            for(index = 0; index < NUM_SPECIES; index++)
            {
                gMain_saveData.pokedexFlags[index] = gPokedexFlags[index];
            }
            SaveFile_WriteToSram();
            FadeOutScreen();
            m4aMPlayAllStop();
            DisableVBlankInterrupts();
            gMain.subState = POKEDEX_STATE_LOAD_GRAPHICS;
            break;
    }
    gPokedexLinkStateTimer++;
}

void Pokedex_DeleteConfirmation(void)
{
    s32 i;

    RenderPokedexSprites();

    if (JOY_NEW(A_BUTTON))
    {
        m4aSongNumStart(SE_MENU_SELECT);
        for (i = 0; i < 0xE1; i++)
        {
            gPokedexFlagExchangeBuffer[i] = 0;
            gPokedexFlags[i] = 0;
        }
        for (i = 0; i < NUM_SPECIES; i++)
        {
            gMain_saveData.pokedexFlags[i] = gPokedexFlags[i];
        }

        gPokedexShowPopupWindow = FALSE;
        gPokedexPopupTypeIndex = POKEDEX_POPUP_TRANSMISSION_CONNECT_PROMPT;
        gPokedexShowButtonPrompt = TRUE;
        gPokedexShowAnimSprite = TRUE;

        SaveFile_WriteToSram();
        FadeOutScreen();
        m4aMPlayAllStop();
        DisableVBlankInterrupts();
        gMain.subState = POKEDEX_STATE_LOAD_GRAPHICS;
    }
    else if (JOY_NEW(B_BUTTON))
    {
        m4aSongNumStart(SE_MENU_CANCEL);
        gPokedexShowPopupWindow = FALSE;
        gPokedexPopupTypeIndex = POKEDEX_POPUP_TRANSMISSION_CONNECT_PROMPT;
        gPokedexShowButtonPrompt = TRUE;
        gPokedexShowAnimSprite = TRUE;
        gMain.subState = POKEDEX_STATE_HANDLE_LIST_INPUT;


    }
}

void Pokedex_ReturnToTitle(void)
{
    FadeOutScreen();
    m4aMPlayAllStop();
    DisableVBlankInterrupts();

    gAutoDisplayTitlescreenMenu = TRUE;
    SetMainGameState(STATE_TITLE);
}

static void PokedexListScrollUp(void)
{
    if (gPokedexScrollWaitFrames != 0)
        return;

    gPokedexSpriteAnimTimer = 0;
    gPokedexSpriteAnimFrame = 0;

    if (gPokedexCursorOffset == 0)
    {
        if (gPokedexListPosition == 0)
        {
            if (!gPokedexScrollActive)
            {
                gPokedexListPosition = LAST_LIST_POSITION;
                gPokedexSelectedMon = gPokedexListEntryCount - 1;
                gPokedexCursorOffset = LIST_ROWS - 1;
                m4aSongNumStart(SE_MENU_MOVE);
            }
        }
        else
        {
            gPokedexListPosition--;
            gPokedexSelectedMon--;
            m4aSongNumStart(SE_MENU_MOVE);
        }

        gPokedexScrollWaitFrames = SCROLL_WAIT_FRAMES;
    }
    else
    {
        m4aSongNumStart(SE_MENU_MOVE);
        gPokedexCursorOffset--;
        gPokedexSelectedMon--;

        gPokedexScrollWaitFrames = SCROLL_WAIT_FRAMES;
    }

    gPokedexScrollActive = TRUE;
}

static void PokedexListScrollDown(void)
{
    if (gPokedexScrollWaitFrames != 0)
        return;

    gPokedexSpriteAnimTimer = 0;
    gPokedexSpriteAnimFrame = 0;
    if (gPokedexCursorOffset == LIST_ROWS - 1)
    {
        if (gPokedexListPosition == LAST_LIST_POSITION)
        {
            if (!gPokedexScrollActive)
            {
                gPokedexListPosition = 0;
                gPokedexSelectedMon = 0;
                gPokedexCursorOffset = 0;
                m4aSongNumStart(SE_MENU_MOVE);
            }
        }
        else
        {
            gPokedexListPosition++;
            gPokedexSelectedMon++;
            m4aSongNumStart(SE_MENU_MOVE);
        }

        gPokedexScrollWaitFrames = SCROLL_WAIT_FRAMES;
    }
    else
    {
        m4aSongNumStart(SE_MENU_MOVE);
        gPokedexCursorOffset++;
        gPokedexSelectedMon++;
        gPokedexScrollWaitFrames = SCROLL_WAIT_FRAMES;
    }

    gPokedexScrollActive = TRUE;
}

static void PokedexListScrollUpFast(void)
{
    if (gPokedexScrollWaitFrames != 0)
        return;

    gPokedexSpriteAnimTimer = 0;
    gPokedexSpriteAnimFrame = 0;
    if (gPokedexListPosition == 0)
        return;

    m4aSongNumStart(SE_MENU_MOVE);
    gPokedexListPosition -= LIST_ROWS;
    if (gPokedexListPosition < 0)
        gPokedexListPosition = 0;

    gPokedexSelectedMon = gPokedexListPosition + gPokedexCursorOffset;
    gPokedexScrollWaitFrames = SCROLL_WAIT_FRAMES;
}

static void PokedexListScrollDownFast(void)
{
    if (gPokedexScrollWaitFrames != 0)
        return;

    gPokedexSpriteAnimTimer = 0;
    gPokedexSpriteAnimFrame = 0;
    if (gPokedexListPosition == LAST_LIST_POSITION)
        return;

    m4aSongNumStart(SE_MENU_MOVE);
    gPokedexListPosition += LIST_ROWS;
    if (gPokedexListPosition > LAST_LIST_POSITION)
        gPokedexListPosition = LAST_LIST_POSITION;

    gPokedexSelectedMon = gPokedexListPosition + gPokedexCursorOffset;
    gPokedexScrollWaitFrames = SCROLL_WAIT_FRAMES;
}

void Pokedex_CheckDeleteKeyComboPressed(void)
{
     // To delete save file, press R_BUTTON 3 times while holding L_BUTTON And DPAD_LEFT.
    if (JOY_HELD(L_BUTTON | DPAD_LEFT) == (L_BUTTON | DPAD_LEFT) && JOY_NEW(R_BUTTON))
    {
        gPokedex_EraseSaveDataAccessCounter = 40;
        if (++gPokedex_EraseSaveDataAccessStep == 3)
        {
            gPokedex_EraseSaveDataAccessStep = 0;
            gPokedex_EraseSaveDataAccessCounter = 0;
            m4aSongNumStart(SE_MENU_POPUP_OPEN);
            gPokedexShowPopupWindow = TRUE;
            gPokedexPopupTypeIndex = POKEDEX_POPUP_DELETE_CONFIRMATION_PROMPT;
            gPokedexShowButtonPrompt = FALSE;
            gPokedexShowAnimSprite = FALSE;
            gMain.subState = POKEDEX_STATE_DELETE_CONFIRMATION;
        }
    }

    if (gPokedex_EraseSaveDataAccessCounter > 0)
    {
        if (--gPokedex_EraseSaveDataAccessCounter <= 0)
        {
            gPokedex_EraseSaveDataAccessCounter = 0;
            gPokedex_EraseSaveDataAccessStep = 0;
        }
    }
}

void UpdateMonSpriteVisibility(void)
{
    if (gPokedexFlags[gPokedexSelectedMon] == SPECIES_DEX_CAUGHT)
    {
        if (gDexAnimationIx[gPokedexSelectedMon] == -1)
        {
            gPokedexShowAnimSprite = FALSE;
            gPokedexShowPortrait = TRUE;
            gPokedexShowCatchHatch[SHOW_IX_CATCH_SPRITE] = FALSE;
            gPokedexShowCatchHatch[SHOW_IX_HATCH_SPRITE] = FALSE;
            gPokedexSpriteAnimTimer = 0;
            gPokedexSpriteAnimFrame = 0;
        }
        else if (gDexAnimationIx[gPokedexSelectedMon] < 100)
        {
            gPokedexShowAnimSprite = FALSE;
            gPokedexShowPortrait = FALSE;
            gPokedexShowCatchHatch[SHOW_IX_CATCH_SPRITE] = TRUE;
            gPokedexShowCatchHatch[SHOW_IX_HATCH_SPRITE] = FALSE;
        }
        else
        {
            gPokedexShowAnimSprite = FALSE;
            gPokedexShowPortrait = FALSE;
            gPokedexShowCatchHatch[SHOW_IX_CATCH_SPRITE] = FALSE;
            gPokedexShowCatchHatch[SHOW_IX_HATCH_SPRITE] = TRUE;
        }
    }
    else
    {
        gPokedexShowAnimSprite = FALSE;
        gPokedexShowPortrait = TRUE;
        gPokedexShowCatchHatch[SHOW_IX_CATCH_SPRITE] = FALSE;
        gPokedexShowCatchHatch[SHOW_IX_HATCH_SPRITE] = FALSE;
    }
}

u8 GetSelectedMonSpriteType(void)
{
    if (gPokedexFlags[gPokedexSelectedMon] == SPECIES_DEX_CAUGHT && gDexAnimationIx[gPokedexSelectedMon] != -1)
    {
        if (gDexAnimationIx[gPokedexSelectedMon] < 100)
            return 1;

        return 2;
    }

    return 0;
}

void RefreshPokedexListDisplay(void)
{
    PrintDexNumbersFromListPosition(gPokedexListPosition);
    PrintCaughtBallFromListPosition(gPokedexListPosition);
#ifdef JVPPBPORT
    PortCaptureSecondPage(FALSE);
#endif
    PrintSelectedMonDexNum(gPokedexSelectedMon);
    LoadMonPortrait(gPokedexSelectedMon);
    LoadMonAnimationSprite(gPokedexSelectedMon);
    CheckMonHasAnimation(gPokedexSelectedMon);
    gPokedexAnimatedIconTimer = 0;
    gPokedexAnimatedIconFrame = 0;
}

void RefreshMonPreviewSprite(void)
{
    if (GetSelectedMonSpriteType() == 0)
        LoadMonPortrait(gPokedexSelectedMon);
    else
        LoadMonAnimationSprite(gPokedexSelectedMon);
}

void RenderPokedexSprites(void)
{
    int i;
    struct SpriteGroup *group0;
    struct SpriteGroup *group1;
    struct SpriteGroup *group2;
    struct SpriteGroup *group3;
    struct SpriteGroup *group4;
    struct SpriteGroup *group5;
    struct SpriteGroup *group6;
    struct SpriteGroup *group7;
    struct SpriteGroup *group8;
    struct SpriteGroup *group9;
    struct OamDataSimple *groupOam;
    const struct SpriteSet *spriteSet;
    int var0, var1;

    group0 = &gMain.spriteGroups[SG_POKEDEX_CURSOR_HAND];
    group1 = &gMain.spriteGroups[SG_POKEDEX_SCROLL_INDICATOR];
    group2 = &gMain.spriteGroups[SG_POKEDEX_MON_PORTRAIT];
    group3 = &gMain.spriteGroups[SG_POKEDEX_CATCH_SPRITE_ANIMATION];
    group4 = &gMain.spriteGroups[SG_POKEDEX_HATCH_SPRITE_ANIMATION];
    group5 = &gMain.spriteGroups[SG_POKEDEX_PRESS_SELECT_TEXT_BASE + gPokedexAnimatedIconFrame];
    group6 = &gMain.spriteGroups[SG_POKEDEX_POPUP_LIST_BASE + gPokedexPopupTypeIndex];
    group7 = &gMain.spriteGroups[SG_POKEDEX_PRESS_START_0 + gPokedexButtonPromptFrame];
    group8 = &gMain.spriteGroups[SG_POKEDEX_COMPLETION_BADGE];
    group9 = &gMain.spriteGroups[SG_POKEDEX_TEXT_PAGE_CHANGE_ARROW_BASE + gPokedexSpriteIndexBase * 2 + gPokedexPageIndicatorBlink];

    group0->active = TRUE;
    group1->active = TRUE;
    group2->active = gPokedexShowPortrait;
    group3->active = gPokedexShowCatchHatch[SHOW_IX_CATCH_SPRITE];
    group4->active = gPokedexShowCatchHatch[SHOW_IX_HATCH_SPRITE];
    group5->active = gPokedexShowAnimSprite;
    group6->active = gPokedexShowPopupWindow;
#ifdef JVPPBPORT
    group7->active = FALSE;  // PRESS START TRANS: the port has no link cable
#else
    group7->active = gPokedexShowButtonPrompt;
#endif
    group8->active = gPokedexShowCompletionBadge;
    group9->active = gPokedexShowPageIndicator;
    LoadSpriteSets(gPokedexSpriteSets, 29, group0);

    group0->baseX = 20 + gPokedexCursorBlinkOffset;
    group0->baseY = 84 + gPokedexCursorOffset * 16;
    groupOam = &group0->oam[0];
    gOamBuffer[groupOam->oamId].priority = 3;
    gOamBuffer[groupOam->oamId].x = groupOam->xOffset + group0->baseX;
    gOamBuffer[groupOam->oamId].y = groupOam->yOffset + group0->baseY;

    if (gPokedexSelectedMon < 200)
        gPokedexScrollbarY = 86 + gPokedexSelectedMon / 3;
    else
        gPokedexScrollbarY = 152;

    group1->baseX = 13;
    group1->baseY = gPokedexScrollbarY;
    groupOam = &group1->oam[0];
    gOamBuffer[groupOam->oamId].priority = 3;
    gOamBuffer[groupOam->oamId].x = groupOam->xOffset + group1->baseX;
    gOamBuffer[groupOam->oamId].y = groupOam->yOffset + group1->baseY;
#ifdef JVPPBPORT
    PortDexPlaceSprites(group0, group1);
#endif

    if (group2->active == TRUE)
    {
        group2->baseX = 36;
        group2->baseY = 36;
        for (i = 0; i < 6; i++)
        {
            groupOam = &group2->oam[i];
            gOamBuffer[groupOam->oamId].priority = 1;
            gOamBuffer[groupOam->oamId].x = groupOam->xOffset + group2->baseX;
            gOamBuffer[groupOam->oamId].y = groupOam->yOffset + group2->baseY;
        }
    }

    if (group3->active == TRUE)
    {
        group3->baseX = 36;
        group3->baseY = 28;
        for (i = 0; i < 4; i++)
        {
            groupOam = &group3->oam[i];
            gOamBuffer[groupOam->oamId].priority = 1;
            gOamBuffer[groupOam->oamId].tileNum = gPokedexAnimBaseTileNums[gPokedexSpriteCategory][i] +
                                                  gPokedexCatchAnimTileOffsets[gPokedexSpriteCategory][gPokedexSpriteAnimFrame];
            gOamBuffer[groupOam->oamId].x = groupOam->xOffset + group3->baseX;
            gOamBuffer[groupOam->oamId].y = groupOam->yOffset + group3->baseY;
        }

        if (++gPokedexSpriteAnimTimer > 14)
        {
            gPokedexSpriteAnimTimer = 0;
            if (++gPokedexSpriteAnimFrame > 8)
                gPokedexSpriteAnimFrame = 0;
        }
    }

    if (group4->active == TRUE)
    {
        if (gPokedexCatchAnimIndices[gPokedexSelectedMon] == -1)
        {
            var0 = 1;
            var1 = 36;
        }
        else
        {
            var0 = 2;
            var1 = 48;
        }

        group4->baseX = 48;
        group4->baseY = 40;
        for (i = 0; i < 4; i++)
        {
            groupOam = &group4->oam[i];
            gOamBuffer[groupOam->oamId].priority = 1;
            gOamBuffer[groupOam->oamId].tileNum = gPokedexAnimBaseTileNums[var0][i] + gPokedexHatchAnimTileOffsets[var0][gPokedexSpriteAnimFrame];
            gOamBuffer[groupOam->oamId].x = groupOam->xOffset + group4->baseX;
            gOamBuffer[groupOam->oamId].y = groupOam->yOffset + group4->baseY;
        }

        if (++gPokedexSpriteAnimTimer > gPokedexAnimFrameDurations[var0][gPokedexSpriteAnimFrame])
        {
            gPokedexSpriteAnimTimer = 0;
            if (++gPokedexSpriteAnimFrame > var1)
                gPokedexSpriteAnimFrame = 0;
        }
    }

    if (group5->active == TRUE)
    {
        group5->baseX = 36;
        group5->baseY = 64;
        spriteSet = gPokedexSpriteSets[5 + gPokedexAnimatedIconFrame];
        for (i = 0; i < spriteSet->count; i++)
        {
            groupOam = &group5->oam[i];
            gOamBuffer[groupOam->oamId].priority = 1;
            gOamBuffer[groupOam->oamId].x = groupOam->xOffset + group5->baseX;
            gOamBuffer[groupOam->oamId].y = groupOam->yOffset + group5->baseY;
        }

        if (++gPokedexAnimatedIconTimer > 8)
        {
            gPokedexAnimatedIconTimer = 0;
            if (++gPokedexAnimatedIconFrame > 11)
                gPokedexAnimatedIconFrame = 0;
        }
    }

    if (group6->active == TRUE)
    {
        if (gPokedexPopupTypeIndex == POKEDEX_POPUP_TRANSMISSION_CONNECT_PROMPT ||
            gPokedexPopupTypeIndex == POKEDEX_POPUP_DELETE_CONFIRMATION_PROMPT)
        {
            group6->baseX = 120;
            group6->baseY = 100;
        }
        else
        {
            group6->baseX = 120;
            group6->baseY = 80;
        }

        spriteSet = gPokedexSpriteSets[17 + gPokedexPopupTypeIndex];
        for (i = 0; i < spriteSet->count; i++)
        {
            groupOam = &group6->oam[i];
            gOamBuffer[groupOam->oamId].priority = 0;
            gOamBuffer[groupOam->oamId].x = groupOam->xOffset + group6->baseX;
            gOamBuffer[groupOam->oamId].y = groupOam->yOffset + group6->baseY;
        }
    }

    if (group7->active == TRUE)
    {
        group7->baseX = 158;
        group7->baseY = 148;
        spriteSet = gPokedexSpriteSets[22 + gPokedexButtonPromptFrame];
        for (i = 0; i < spriteSet->count; i++)
        {
            groupOam = &group7->oam[i];
            gOamBuffer[groupOam->oamId].x = groupOam->xOffset + group7->baseX;
            gOamBuffer[groupOam->oamId].y = groupOam->yOffset + group7->baseY;
        }
    }

    if (group8->active == TRUE)
    {
        group8->baseX = 168;
        group8->baseY = 84;
        for (i = 0; i < 3; i++)
        {
            groupOam = &group8->oam[i];
            gOamBuffer[groupOam->oamId].priority = 3;
            gOamBuffer[groupOam->oamId].x = groupOam->xOffset + group8->baseX;
            gOamBuffer[groupOam->oamId].y = groupOam->yOffset + group8->baseY;
        }
    }

    if (group9->active == TRUE)
    {
        group9->baseX = 120;
        group9->baseY = 132;
        groupOam = &group9->oam[0];
        gOamBuffer[groupOam->oamId].x = groupOam->xOffset + group9->baseX;
        gOamBuffer[groupOam->oamId].y = groupOam->yOffset + group9->baseY;
    }

    if (++gPokedexBlinkTimer > 12)
    {
        gPokedexBlinkTimer = 0;
        gPokedexCursorBlinkOffset = 1 - gPokedexCursorBlinkOffset;
        gPokedexButtonPromptFrame = 1 - gPokedexButtonPromptFrame;
    }

    group5->active = FALSE;
    group6->active = FALSE;
    group7->active = FALSE;
    group9->active = FALSE;
}

static void RenderLinkGraphics(void)
{
    int i;
    struct SpriteGroup *group0;
    struct SpriteGroup *group1;
    struct SpriteGroup *group2;
    struct SpriteGroup *group3;
    struct SpriteGroup *group4;
    struct SpriteGroup *group5;
    struct SpriteGroup *group6;
    struct SpriteGroup *group7;
    struct OamDataSimple *groupOam;
    const struct SpriteSet *spriteSet;

    group0 = &gMain.spriteGroups[SG_POKEDEX_CURSOR_HAND];
    group1 = &gMain.spriteGroups[SG_POKEDEX_SCROLL_INDICATOR];
    group2 = &gMain.spriteGroups[SG_POKEDEX_MON_PORTRAIT];
    group3 = &gMain.spriteGroups[SG_POKEDEX_CATCH_SPRITE_ANIMATION];
    group4 = &gMain.spriteGroups[SG_POKEDEX_HATCH_SPRITE_ANIMATION];
    group6 = &gMain.spriteGroups[SG_POKEDEX_PRESS_SELECT_TEXT_BASE + gPokedexAnimatedIconFrame];
    group7 = &gMain.spriteGroups[SG_POKEDEX_POPUP_LIST_BASE + gPokedexPopupTypeIndex];
    group5 = &gMain.spriteGroups[SG_POKEDEX_COMPLETION_BADGE];

    group0->active = TRUE;
    group1->active = TRUE;
    group2->active = gPokedexShowPortrait;
    group3->active = gPokedexShowCatchHatch[SHOW_IX_CATCH_SPRITE];
    group4->active = gPokedexShowCatchHatch[SHOW_IX_HATCH_SPRITE];
    group6->active = FALSE;
    group7->active = gPokedexShowPopupWindow;
    group5->active = gPokedexShowCompletionBadge;
    LoadSpriteSetsWithCpuCopy(gPokedexSpriteSets, 29, group0);

    group0->baseX = 20 + gPokedexCursorBlinkOffset;
    group0->baseY = 84 + gPokedexCursorOffset * 16;
    groupOam = &group0->oam[0];
    gOamBuffer[groupOam->oamId].priority = 2;
    gOamBuffer[groupOam->oamId].x = groupOam->xOffset + group0->baseX;
    gOamBuffer[groupOam->oamId].y = groupOam->yOffset + group0->baseY;

    gPokedexScrollbarY = 86 + gPokedexSelectedMon / 3;
    group1->baseX = 13;
    group1->baseY = gPokedexScrollbarY;
    groupOam = &group1->oam[0];
    gOamBuffer[groupOam->oamId].priority = 2;
    gOamBuffer[groupOam->oamId].x = groupOam->xOffset + group1->baseX;
    gOamBuffer[groupOam->oamId].y = groupOam->yOffset + group1->baseY;
#ifdef JVPPBPORT
    PortDexPlaceSprites(group0, group1);
#endif

    if (group2->active == TRUE)
    {
        group2->baseX = 36;
        group2->baseY = 36;
        for (i = 0; i < 6; i++)
        {
            groupOam = &group2->oam[i];
            gOamBuffer[groupOam->oamId].x = groupOam->xOffset + group2->baseX;
            gOamBuffer[groupOam->oamId].y = groupOam->yOffset + group2->baseY;
        }
    }

    if (group3->active == TRUE)
    {
        group3->baseX = 36;
        group3->baseY = 28;
        for (i = 0; i < 4; i++)
        {
            groupOam = &group3->oam[i];
            gOamBuffer[groupOam->oamId].tileNum = gPokedexAnimBaseTileNums[gPokedexSpriteCategory][i] +
                                                  gPokedexCatchAnimTileOffsets[gPokedexSpriteCategory][gPokedexSpriteAnimFrame];
            gOamBuffer[groupOam->oamId].x = groupOam->xOffset + group3->baseX;
            gOamBuffer[groupOam->oamId].y = groupOam->yOffset + group3->baseY;
        }
    }

    if (group4->active == TRUE)
    {
        group4->baseX = 48;
        group4->baseY = 40;
        for (i = 0; i < 4; i++)
        {
            groupOam = &group4->oam[i];
            gOamBuffer[groupOam->oamId].tileNum = gPokedexAnimBaseTileNums[1][i] +
                                                  gPokedexCatchAnimTileOffsets[1][gPokedexSpriteAnimFrame];
            gOamBuffer[groupOam->oamId].x = groupOam->xOffset + group4->baseX;
            gOamBuffer[groupOam->oamId].y = groupOam->yOffset + group4->baseY;
        }
    }

    if (group6->active == TRUE)
    {
        group6->baseX = 36;
        group6->baseY = 64;
        spriteSet = gPokedexSpriteSets[5 + gPokedexAnimatedIconFrame];
        for (i = 0; i < spriteSet->count; i++)
        {
            groupOam = &group6->oam[i];
            gOamBuffer[groupOam->oamId].x = groupOam->xOffset + group6->baseX;
            gOamBuffer[groupOam->oamId].y = groupOam->yOffset + group6->baseY;
        }
    }

    if (group7->active == TRUE)
    {
        if (gPokedexPopupTypeIndex == POKEDEX_POPUP_TRANSMISSION_CONNECT_PROMPT ||
            gPokedexPopupTypeIndex == POKEDEX_POPUP_DELETE_CONFIRMATION_PROMPT)
        {
            group7->baseX = 120;
            group7->baseY = 100;
        }
        else
        {
            group7->baseX = 120;
            group7->baseY = 80;
        }

        spriteSet = gPokedexSpriteSets[17 + gPokedexPopupTypeIndex];
        for (i = 0; i < spriteSet->count; i++)
        {
            groupOam = &group7->oam[i];
            gOamBuffer[groupOam->oamId].x = groupOam->xOffset + group7->baseX;
            gOamBuffer[groupOam->oamId].y = groupOam->yOffset + group7->baseY;
        }
    }

    if (group5->active == TRUE)
    {
        group5->baseX = 168;
        group5->baseY = 84;
        for (i = 0; i < 3; i++)
        {
            groupOam = &group5->oam[i];
            gOamBuffer[groupOam->oamId].priority = 3;
            gOamBuffer[groupOam->oamId].x = groupOam->xOffset + group5->baseX;
            gOamBuffer[groupOam->oamId].y = groupOam->yOffset + group5->baseY;
        }
    }

    group7->active = FALSE;
}

static void InitLinkTransferState(void)
{
    gLinkSendBuffer[0] = 0xDDDD;
    gPokedexLinkChunkIndex = 1;
    gPokedexLinkSendCounter = 1;
    gPokedexLinkTransferPhase = 0;
    gPokedexMasterHandshakeState = 0;
    gPokedexClientHandshakeState = 0;
    gPokedexHandshakeRetryCount = 0;
}

static s16 Pokedex_ProcessLinkExchange(void)
{
    s16 result;

    if (gLink.isMaster)
        result = MasterReceivePokedexFlags();
    else
        result = ClientReceivePokedexFlags();

    return result;
}

s16 SendLinkPokedexData(void)
{
    s16 result;

    if (gLink.isMaster)
        result = MasterSendPokedexFlags();
    else
        result = ClientSendPokedexFlags();

    return result;
}

static int MasterSendPokedexFlags(void)
{
    int i;
    u16 var0;

    if (gPokedexLinkTransferPhase == 0)
    {
        switch (gPokedexMasterHandshakeState)
        {
            case 0:
                if (JOY_NEW(A_BUTTON))
                {
                    gLinkSendBuffer[0] = 0xFEFE;
                }
                else
                {
                    for (i = 0; i < 8; i++)
                        gLinkSendBuffer[i] = 0;
                }
                return 0;
            case 1:
                gLinkSendBuffer[0] = 0xECEC;
                return 0;
        }
        return 0;
    }
    else if (gPokedexLinkSendCounter < 8)
    {
        for (i = 0; i < 8; i++)
            gLinkSendBuffer[i] = 0xDDDD;
    }
    else if (gPokedexLinkSendCounter < 16)
    {
        var0 = (gPokedexLinkSendCounter - 8) * 28;
        gLinkSendBuffer[0] = gPokedexLinkSendCounter;
        gLinkSendBuffer[1] = gPokedexFlags[var0] |
                               (gPokedexFlags[var0 + 1] << 4) |
                               (gPokedexFlags[var0 + 2] << 8) |
                               (gPokedexFlags[var0 + 3] << 12);
        gLinkSendBuffer[2] =  gPokedexFlags[var0 + 4] |
                               (gPokedexFlags[var0 + 5] << 4) |
                               (gPokedexFlags[var0 + 6] << 8) |
                               (gPokedexFlags[var0 + 7] << 12);
        gLinkSendBuffer[3] =  gPokedexFlags[var0 + 8] |
                               (gPokedexFlags[var0 + 9] << 4) |
                               (gPokedexFlags[var0 + 10] << 8) |
                               (gPokedexFlags[var0 + 11] << 12);
        gLinkSendBuffer[4] =  gPokedexFlags[var0 + 12] |
                               (gPokedexFlags[var0 + 13] << 4) |
                               (gPokedexFlags[var0 + 14] << 8) |
                               (gPokedexFlags[var0 + 15] << 12);
        gLinkSendBuffer[5] =  gPokedexFlags[var0 + 16] |
                               (gPokedexFlags[var0 + 17] << 4) |
                               (gPokedexFlags[var0 + 18] << 8) |
                               (gPokedexFlags[var0 + 19] << 12);
        gLinkSendBuffer[6] =  gPokedexFlags[var0 + 20] |
                               (gPokedexFlags[var0 + 21] << 4) |
                               (gPokedexFlags[var0 + 22] << 8) |
                               (gPokedexFlags[var0 + 23] << 12);
        gLinkSendBuffer[7] =  gPokedexFlags[var0 + 24] |
                               (gPokedexFlags[var0 + 25] << 4) |
                               (gPokedexFlags[var0 + 26] << 8) |
                               (gPokedexFlags[var0 + 27] << 12);
    }
    else
    {
        for (i = 0; i < 8; i++)
            gLinkSendBuffer[i] = 0;
    }

    gPokedexLinkSendCounter++;
    return 0;
}

static int ClientSendPokedexFlags(void)
{
    int i;
    u16 var0;

    if (gPokedexLinkTransferPhase == 0)
    {
        switch (gPokedexClientHandshakeState)
        {
            case 0:
                if (JOY_NEW(A_BUTTON))
                {
                    gLinkSendBuffer[0] = 0xFEFE;
                }
                else
                {
                    for (i = 0; i < 8; i++)
                        gLinkSendBuffer[i] = 0;
                }
                return 0;
            case 1:
                gLinkSendBuffer[0] = 0xECEC;
                return 0;
        }
        return 0;
    }
    else if (gPokedexLinkSendCounter < 8)
    {
        for (i = 0; i < 8; i++)
            gLinkSendBuffer[i] = 0xDDDD;
    }
    else if (gPokedexLinkSendCounter < 16)
    {
        var0 = (gPokedexLinkSendCounter - 8) * 28;
        gLinkSendBuffer[0] = gPokedexLinkSendCounter;
        gLinkSendBuffer[1] = gPokedexFlags[var0] |
                               (gPokedexFlags[var0 + 1] << 4) |
                               (gPokedexFlags[var0 + 2] << 8) |
                               (gPokedexFlags[var0 + 3] << 12);
        gLinkSendBuffer[2] =  gPokedexFlags[var0 + 4] |
                               (gPokedexFlags[var0 + 5] << 4) |
                               (gPokedexFlags[var0 + 6] << 8) |
                               (gPokedexFlags[var0 + 7] << 12);
        gLinkSendBuffer[3] =  gPokedexFlags[var0 + 8] |
                               (gPokedexFlags[var0 + 9] << 4) |
                               (gPokedexFlags[var0 + 10] << 8) |
                               (gPokedexFlags[var0 + 11] << 12);
        gLinkSendBuffer[4] =  gPokedexFlags[var0 + 12] |
                               (gPokedexFlags[var0 + 13] << 4) |
                               (gPokedexFlags[var0 + 14] << 8) |
                               (gPokedexFlags[var0 + 15] << 12);
        gLinkSendBuffer[5] =  gPokedexFlags[var0 + 16] |
                               (gPokedexFlags[var0 + 17] << 4) |
                               (gPokedexFlags[var0 + 18] << 8) |
                               (gPokedexFlags[var0 + 19] << 12);
        gLinkSendBuffer[6] =  gPokedexFlags[var0 + 20] |
                               (gPokedexFlags[var0 + 21] << 4) |
                               (gPokedexFlags[var0 + 22] << 8) |
                               (gPokedexFlags[var0 + 23] << 12);
        gLinkSendBuffer[7] =  gPokedexFlags[var0 + 24] |
                               (gPokedexFlags[var0 + 25] << 4) |
                               (gPokedexFlags[var0 + 26] << 8) |
                               (gPokedexFlags[var0 + 27] << 12);
    }
    else
    {
        for (i = 0; i < 8; i++)
            gLinkSendBuffer[i] = 0;
    }

    gPokedexLinkSendCounter++;
    return 0;
}

//Link transfer as host (See ClientReceivePokedexFlags for client)
int MasterReceivePokedexFlags(void)
{
    int i, j;
    u16 var0;
    u16 receivedDexFlags[28];

    if (gPokedexLinkTransferPhase == 0)
    {
        switch (gPokedexMasterHandshakeState)
        {
            case 0:
                if (gLinkRecvBuffer[0][0] == 0xFEFE || gLinkRecvBuffer[0][1] == 0xFEFE)
                {
                    gPokedexMasterHandshakeState = 1;
                }
                break;
            case 1:
                if (gLinkRecvBuffer[0][0] == 0xECEC && gLinkRecvBuffer[0][1] == 0xECEC)
                {
                    gPokedexLinkTransferPhase = -1;
                    gPokedexPopupTypeIndex = POKEDEX_POPUP_TRANSMITTING_ACTIVE;
                    gPokedexLinkSendCounter = 1;
                }
                else if (++gPokedexHandshakeRetryCount > 10)
                {
                    return 1;
                }
                break;
        }
    }
    else
    {
        u16 var1 = gLinkRecvBuffer[0][1] - 8;
         if (var1 > 7)
            return 0;

        gPokedexLinkChunkIndex = gLinkRecvBuffer[0][1];
        receivedDexFlags[0]  =  gLinkRecvBuffer[0][3]  & 0xF;
        receivedDexFlags[1]  = (gLinkRecvBuffer[0][3]  & 0xF0)   >> 4;
        receivedDexFlags[2]  = (gLinkRecvBuffer[0][3]  & 0xF00)  >> 8;
        receivedDexFlags[3]  = (gLinkRecvBuffer[0][3]  & 0xF000) >> 12;
        receivedDexFlags[4]  =  gLinkRecvBuffer[0][5]  & 0xF;
        receivedDexFlags[5]  = (gLinkRecvBuffer[0][5]  & 0xF0)   >> 4;
        receivedDexFlags[6]  = (gLinkRecvBuffer[0][5]  & 0xF00)  >> 8;
        receivedDexFlags[7]  = (gLinkRecvBuffer[0][5]  & 0xF000) >> 12;
        receivedDexFlags[8]  =  gLinkRecvBuffer[0][7]  & 0xF;
        receivedDexFlags[9]  = (gLinkRecvBuffer[0][7]  & 0xF0)   >> 4;
        receivedDexFlags[10] = (gLinkRecvBuffer[0][7]  & 0xF00)  >> 8;
        receivedDexFlags[11] = (gLinkRecvBuffer[0][7]  & 0xF000) >> 12;
        receivedDexFlags[12] =  gLinkRecvBuffer[0][9]  & 0xF;
        receivedDexFlags[13] = (gLinkRecvBuffer[0][9]  & 0xF0)   >> 4;
        receivedDexFlags[14] = (gLinkRecvBuffer[0][9]  & 0xF00)  >> 8;
        receivedDexFlags[15] = (gLinkRecvBuffer[0][9]  & 0xF000) >> 12;
        receivedDexFlags[16] =  gLinkRecvBuffer[0][11] & 0xF;
        receivedDexFlags[17] = (gLinkRecvBuffer[0][11] & 0xF0)   >> 4;
        receivedDexFlags[18] = (gLinkRecvBuffer[0][11] & 0xF00)  >> 8;
        receivedDexFlags[19] = (gLinkRecvBuffer[0][11] & 0xF000) >> 12;
        receivedDexFlags[20] =  gLinkRecvBuffer[0][13] & 0xF;
        receivedDexFlags[21] = (gLinkRecvBuffer[0][13] & 0xF0)   >> 4;
        receivedDexFlags[22] = (gLinkRecvBuffer[0][13] & 0xF00)  >> 8;
        receivedDexFlags[23] = (gLinkRecvBuffer[0][13] & 0xF000) >> 12;
        receivedDexFlags[24] =  gLinkRecvBuffer[0][15] & 0xF;
        receivedDexFlags[25] = (gLinkRecvBuffer[0][15] & 0xF0)   >> 4;
        receivedDexFlags[26] = (gLinkRecvBuffer[0][15] & 0xF00)  >> 8;
        receivedDexFlags[27] = (gLinkRecvBuffer[0][15] & 0xF000) >> 12;

        for (i = 0; i < 28; i++)
        {
            var0 = (gPokedexLinkChunkIndex - 8) * 28 + i;
            if (gPokedexFlags[var0] == SPECIES_DEX_UNSEEN && receivedDexFlags[i] == SPECIES_DEX_CAUGHT)
                gPokedexFlagExchangeBuffer[var0] = SPECIES_DEX_SHARED;
            else if (gPokedexFlags[var0] == SPECIES_DEX_SEEN && receivedDexFlags[i] == SPECIES_DEX_CAUGHT)
                gPokedexFlagExchangeBuffer[var0] = SPECIES_DEX_SHARED_AND_SEEN;
        }

        if (gPokedexLinkChunkIndex == 15)
            return -1;
    }

    for (i = 0; i < 8; i++)
    {
        for (j = 0; j < 2; j++)
            gLinkRecvBuffer[i][j] = 0;
    }

    return 0;
}

//Link transfer as client (See MasterReceivePokedexFlags for host)
static int ClientReceivePokedexFlags(void)
{
    int i, j;
    u16 var0;
    u16 receivedDexFlags[28];

    if (gPokedexLinkTransferPhase == 0)
    {
        switch (gPokedexClientHandshakeState)
        {
            case 0:
                if (gLinkRecvBuffer[0][0] == 0xFEFE || gLinkRecvBuffer[0][1] == 0xFEFE)
                {
                    gPokedexClientHandshakeState = 1;
                }
                break;
            case 1:
                if (gLinkRecvBuffer[0][0] == 0xECEC && gLinkRecvBuffer[0][1] == 0xECEC)
                {
                    gPokedexLinkTransferPhase = -1;
                    gPokedexPopupTypeIndex = POKEDEX_POPUP_TRANSMITTING_ACTIVE;
                    gPokedexLinkSendCounter = 1;
                }
                else if (++gPokedexHandshakeRetryCount > 10)
                {
                    return 1;
                }
                break;
        }
    }
    else
    {
        u16 var1 = gLinkRecvBuffer[0][0] - 8;
         if (var1 > 7)
            return 0;

        gPokedexLinkChunkIndex = gLinkRecvBuffer[0][0];
        receivedDexFlags[0]  =  gLinkRecvBuffer[0][2]  & 0xF;
        receivedDexFlags[1]  = (gLinkRecvBuffer[0][2]  & 0xF0)   >> 4;
        receivedDexFlags[2]  = (gLinkRecvBuffer[0][2]  & 0xF00)  >> 8;
        receivedDexFlags[3]  = (gLinkRecvBuffer[0][2]  & 0xF000) >> 12;
        receivedDexFlags[4]  =  gLinkRecvBuffer[0][4]  & 0xF;
        receivedDexFlags[5]  = (gLinkRecvBuffer[0][4]  & 0xF0)   >> 4;
        receivedDexFlags[6]  = (gLinkRecvBuffer[0][4]  & 0xF00)  >> 8;
        receivedDexFlags[7]  = (gLinkRecvBuffer[0][4]  & 0xF000) >> 12;
        receivedDexFlags[8]  =  gLinkRecvBuffer[0][6]  & 0xF;
        receivedDexFlags[9]  = (gLinkRecvBuffer[0][6]  & 0xF0)   >> 4;
        receivedDexFlags[10] = (gLinkRecvBuffer[0][6]  & 0xF00)  >> 8;
        receivedDexFlags[11] = (gLinkRecvBuffer[0][6]  & 0xF000) >> 12;
        receivedDexFlags[12] =  gLinkRecvBuffer[0][8]  & 0xF;
        receivedDexFlags[13] = (gLinkRecvBuffer[0][8]  & 0xF0)   >> 4;
        receivedDexFlags[14] = (gLinkRecvBuffer[0][8]  & 0xF00)  >> 8;
        receivedDexFlags[15] = (gLinkRecvBuffer[0][8]  & 0xF000) >> 12;
        receivedDexFlags[16] =  gLinkRecvBuffer[0][10] & 0xF;
        receivedDexFlags[17] = (gLinkRecvBuffer[0][10] & 0xF0)   >> 4;
        receivedDexFlags[18] = (gLinkRecvBuffer[0][10] & 0xF00)  >> 8;
        receivedDexFlags[19] = (gLinkRecvBuffer[0][10] & 0xF000) >> 12;
        receivedDexFlags[20] =  gLinkRecvBuffer[0][12] & 0xF;
        receivedDexFlags[21] = (gLinkRecvBuffer[0][12] & 0xF0)   >> 4;
        receivedDexFlags[22] = (gLinkRecvBuffer[0][12] & 0xF00)  >> 8;
        receivedDexFlags[23] = (gLinkRecvBuffer[0][12] & 0xF000) >> 12;
        receivedDexFlags[24] =  gLinkRecvBuffer[0][14] & 0xF;
        receivedDexFlags[25] = (gLinkRecvBuffer[0][14] & 0xF0)   >> 4;
        receivedDexFlags[26] = (gLinkRecvBuffer[0][14] & 0xF00)  >> 8;
        receivedDexFlags[27] = (gLinkRecvBuffer[0][14] & 0xF000) >> 12;

        for (i = 0; i < 28; i++)
        {
            var0 = (gPokedexLinkChunkIndex - 8) * 28 + i;
            if (gPokedexFlags[var0] == SPECIES_DEX_UNSEEN && receivedDexFlags[i] == SPECIES_DEX_CAUGHT)
                gPokedexFlagExchangeBuffer[var0] = SPECIES_DEX_SHARED;
            else if (gPokedexFlags[var0] == SPECIES_DEX_SEEN && receivedDexFlags[i] == SPECIES_DEX_CAUGHT)
                gPokedexFlagExchangeBuffer[var0] = SPECIES_DEX_SHARED_AND_SEEN;
        }

        if (gPokedexLinkChunkIndex == 15)
            return -1;
    }

    for (i = 0; i < 8; i++)
    {
        for (j = 0; j < 2; j++)
            gLinkRecvBuffer[i][j] = 0;
    }

    return 0;
}

static void PrintSelectedMonDexNum(s16 species)
{
    int i;
    int var0;
    u16 var1;
    u16 var2;

    DmaFill16(3, 0, gTempGfxBuffer, 0x800);
    var0 = 0;
    if (species == SPECIES_JIRACHI)
    {
        if (gPokedexFlags[SPECIES_JIRACHI] != SPECIES_DEX_UNSEEN)
        {
            PrintString(CHAR_2_FONT_1, 1, 5, 2, 1, 2);
            PrintString(CHAR_0_FONT_1, 1, 6, 2, 1, 2);
            PrintString(CHAR_1_FONT_1, 1, 7, 2, 1, 2);
        }
        else
        {
            PrintString(CHAR_SPACE_FONT_1, 1, 5, 2, 1, 2);
            PrintString(CHAR_SPACE_FONT_1, 1, 6, 2, 1, 2);
            PrintString(CHAR_SPACE_FONT_1, 1, 7, 2, 1, 2);
        }
    }
    else
    {
        // Dex number of the selected species
        for (i = 0; i < DEX_NUM_DIGITS; i++)
            PrintString(gPokedexEntries[species].dexNum[i] + 32, 1, i + 5, 2, 1, 2);
    }

    if (gPokedexFlags[species] > SPECIES_DEX_UNSEEN)
    {
        for (i = 0; i < POKEMON_NAME_LENGTH; i++)
        {
            var1 = gPokedexEntries[species].name[i] & ~0xF;
            var2 = gPokedexEntries[species].name[i] & 0xF;
            if (var2 == 0)
                var2 = 4;

            DmaCopy16(3, &gPokedexTextGlyphs_Gfx[var1], gGlyphUpperRowBuffer, 0x20);
            DmaCopy16(3, &gPokedexTextGlyphs_Gfx[var1 + 0x400], gGlyphLowerRowBuffer, 0x20);
            BlitGlyphToTileBuffer(var2, var0, 0);
            var0 += var2;
        }

        CopyBgTilesRect(gTempGfxBuffer, (void *)BG_VRAM_ADDR_POKEDEX_NAME_RECT, 8, 2);
        DmaFill16(3, 0, gTempGfxBuffer, 0x800);
        var0 = 0;
    }
    else
    {
        for (i = 0; i < 10; i++)
            CopyBgTilesRect((void *)&gPokedexTextGlyphs_Gfx[ENGLISH_GLYPHS_START], (void *)BG_VRAM_ADDR_POKEDEX_NAME_RECT + i * TILE_SIZE_4BPP, 1, 2);
    }

    if (gPokedexFlags[species] == SPECIES_DEX_SEEN || gPokedexFlags[species] > SPECIES_DEX_SHARED)
    {
        for (i = 0; i < POKEMON_CATEGORY_NAME_LENGTH; i++)
        {
            var1 = gPokedexEntries[species].category[i] & ~0xF;
            var2 = gPokedexEntries[species].category[i] & 0xF;
            if (var2 == 0)
                var2 = 6;

            DmaCopy16(3, &gPokedexTextGlyphs_Gfx[var1], gGlyphUpperRowBuffer, 0x20);
            DmaCopy16(3, &gPokedexTextGlyphs_Gfx[0x400 + var1], gGlyphLowerRowBuffer, 0x20);
            BlitGlyphToTileBuffer(var2, var0, 0);
            var0 += var2;
        }

        CopyBgTilesRect(gTempGfxBuffer, (void *)BG_VRAM_ADDR_POKEDEX_CATEGORY_RECT, 9, 2);
        DmaFill16(3, 0, gTempGfxBuffer, 0x800);
    }
    else
    {
        for (i = 0; i < 9; i++)
            CopyBgTilesRect((void *)&gPokedexTextGlyphs_Gfx[ENGLISH_GLYPHS_START], (void *)BG_VRAM_ADDR_POKEDEX_CATEGORY_RECT + i * TILE_SIZE_4BPP, 1, 2);
    }

    if (gPokedexFlags[species] == SPECIES_DEX_CAUGHT)
    {
        PrintString(gPokedexEntries[species].heightWeight[0] + 32, 1, 16, 6, 1, 2);
        PrintString(gPokedexEntries[species].heightWeight[1] + 32, 1, 17, 6, 1, 2);
        PrintString(gPokedexEntries[species].heightWeight[2] + 32, 1, 19, 6, 1, 2);
        PrintString(gPokedexEntries[species].heightWeight[3] + 32, 1, 20, 6, 1, 2);
        for (i = 0; i < POKEMON_HEIGHT_WEIGHT_TEXT_LENGTH - 4; i++)
            PrintString(gPokedexEntries[species].heightWeight[4 + i], 1, i + 16, 8, 1, 1);
    }
    else
    {
        PrintString(CHAR_DASH_FONT_1, 1, 16, 6, 1, 2);
        PrintString(CHAR_DASH_FONT_1, 1, 17, 6, 1, 2);
        PrintString(CHAR_DASH_FONT_1, 1, 19, 6, 1, 2);
        PrintString(CHAR_DASH_FONT_1, 1, 20, 6, 1, 2);
        for (i = 0; i < POKEMON_HEIGHT_WEIGHT_TEXT_LENGTH - 4; i++)
            PrintString(CHAR_DASH_FONT_0, 1, i + 16, 8, 1, 1);
    }
}

static void PrintSeenOwnedTotals(s16 seen, s16 owned)
{
    int i;
    int temp;
    int seenDigits[DEX_NUM_DIGITS];
    int ownedDigits[DEX_NUM_DIGITS];

    temp = seen;
    seenDigits[0] = LEAD_DIGIT_100S(temp);
    seenDigits[1] = DIGIT_10S_SCALEDOWN(temp);
    seenDigits[2] = DIGIT_1S(temp);

    temp = owned;
    ownedDigits[0] = LEAD_DIGIT_100S(temp);
    ownedDigits[1] = DIGIT_10S_SCALEDOWN(temp);
    ownedDigits[2] = DIGIT_1S(temp);

    for (i = 0; i < DEX_NUM_DIGITS; i++)
    {
        PrintString(seenDigits[i], 2, i + 25, 15, 1, 1);
        PrintString(ownedDigits[i] + 32, 2, i + 25, 16, 1, 2);
    }
}

void PrintDexNumbersFromListPosition(s16 listPosition)
{
    int i, j;
    int var0;
    u16 var1;
    u16 var2;

    DmaFill16(3, 0, gTempGfxBuffer, 0x800);
    var0 = 0;
    for (i = 0; i < ENTRIES_SHOWN_COUNT; i++)
    {
        if (listPosition + i == SPECIES_JIRACHI)
        {
            if (gPokedexFlags[SPECIES_JIRACHI] != SPECIES_DEX_UNSEEN)
            {
                PrintString(CHAR_2_FONT_1, 2, 8, i * 2 + 10, 1, 2);
                PrintString(CHAR_0_FONT_1, 2, 9, i * 2 + 10, 1, 2);
                PrintString(CHAR_1_FONT_1, 2, 10, i * 2 + 10, 1, 2);
            }
            else
            {
                PrintString(CHAR_SPACE_FONT_1, 2, 8, i * 2 + 10, 1, 2);
                PrintString(CHAR_SPACE_FONT_1, 2, 9, i * 2 + 10, 1, 2);
                PrintString(CHAR_SPACE_FONT_1, 2, 10, i * 2 + 10, 1, 2);
            }
        }
        else
        {
            // Doesn't use listPosition for some reason, despite being the only value passed
            for (j = 0; j < DEX_NUM_DIGITS; j++)
                PrintString(gPokedexEntries[gPokedexListPosition + i].dexNum[j] + 32, 2, j + 8, i * 2 + 10, 1, 2);
        }
    }

    for (i = 0; i < ENTRIES_SHOWN_COUNT; i++)
    {
        if (gPokedexFlags[listPosition + i] > SPECIES_DEX_UNSEEN)
        {
			//This manually builds the tiles needed, for kerning reasons.
			//First 3 bytes point to a tile glyph pair (with an upper and lower half) and
			//the 4th byte is the width needed for that glyph, with the 'space between' included.
			//The 'space' character is 4 px wide.
            for (j = 0; j < POKEMON_NAME_LENGTH; j++)
            {
                // These don't use listPosition for some reason, despite being the only value passed
                var1 = gPokedexEntries[gPokedexListPosition + i].name[j] & ~0xF;
                var2 = gPokedexEntries[gPokedexListPosition + i].name[j] & 0xF;
                if (var2 == 0)
                    var2 = 4;

                DmaCopy16(3, &gPokedexTextGlyphs_Gfx[var1], gGlyphUpperRowBuffer, 0x20);
                DmaCopy16(3, &gPokedexTextGlyphs_Gfx[0x400 + var1], gGlyphLowerRowBuffer, 0x20);
                BlitGlyphToTileBuffer(var2, var0, 0);
                var0 += var2;
            }

            CopyBgTilesRect(gTempGfxBuffer, (void*)VRAM + gPokedexListNameVramOffsets[i], 8, 2);
            DmaFill16(3, 0, gTempGfxBuffer, 0x800);
            var0 = 0;
        }
        else
        {
            for (j = 0; j < 7; j++)
                CopyBgTilesRect((void *)&gPokedexTextGlyphs_Gfx[ENGLISH_GLYPHS_START], (void*)VRAM + gPokedexListNameVramOffsets[i] + j * 0x20, 1, 2);

            CopyBgTilesRect((void *)gPokedexTextGlyphs_Gfx, (void*)VRAM + gPokedexListNameVramOffsets[i] + j * 0x20, 1, 2);
        }
    }
}

static void PrintCaughtBallFromListPosition(s16 position)
{
    int i;
    int var0;

    for (i = 0; i < ENTRIES_SHOWN_COUNT; i++)
    {
        var0 = gPokedexFlags[position + i] == SPECIES_DEX_CAUGHT ? CHAR_BALL_CAUGHT : CHAR_BALL_NOT_CAUGHT;
        PrintString(var0, 1, 4, 10 + i * 2, 2, 2);
    }
}

void LoadMonPortrait(s16 species)
{
    s16 state = gPokedexFlags[gPokedexSelectedMon];
    s16 var1 = species / 15;
    s16 var2 = species % 15;
    switch (state)
    {
        case SPECIES_DEX_UNSEEN:
            CopyBgTilesRect(&gPokedexSprites_Gfx[0x5C00], (void*)OBJ_VRAM_ADDR_POKEDEX_MON_SPRITE_RECT, 24, 1);
            DmaCopy16(3, gPokedexSprites_Pals, OBJ_PLTT_SLOT(PAL_IX_BALL), PLTT_SLOT_SIZE);
            break;
        case SPECIES_DEX_SEEN:
            CopyBgTilesRect(gMonPortraitGroupGfx[var1] + var2 * 0x300, (void*)OBJ_VRAM_ADDR_POKEDEX_MON_SPRITE_RECT, 24, 1);
            DmaCopy16(3, gMonPortraitGroupPals[0][15], OBJ_PLTT_SLOT(PAL_IX_BALL), PLTT_SLOT_SIZE);
            break;
        case SPECIES_DEX_SHARED:
        case SPECIES_DEX_SHARED_AND_SEEN:
            CopyBgTilesRect(gMonPortraitGroupGfx[var1] + var2 * 0x300, (void*)OBJ_VRAM_ADDR_POKEDEX_MON_SPRITE_RECT, 24, 1);
            DarkenPalette(&gMonPortraitGroupPals[var1][var2], (void*)OBJ_PLTT_SLOT(PAL_IX_BALL), 0x20, 0xE);
            break;
        case SPECIES_DEX_CAUGHT:
            CopyBgTilesRect(gMonPortraitGroupGfx[var1] + var2 * 0x300, (void*)OBJ_VRAM_ADDR_POKEDEX_MON_SPRITE_RECT, 24, 1);
            DmaCopy16(3, gMonPortraitGroupPals[var1][var2], OBJ_PLTT_SLOT(PAL_IX_BALL), PLTT_SLOT_SIZE);
            break;
    }
}

void PrintDexDescription(s16 species, u32 page)
{
    int i, j;
    int var0;
    u16 var1;
    u16 var2;

    var0 = 0;
    DmaFill16(3, 0, gTempGfxBuffer, 0x1800);

    //2 pages, 3 rows, 42 chars
    for (i = 0; i < DEX_TEXT_ROWS; i++)
    {
        for (j = 0; j < POKEMON_DEX_LINE_LENGTH; j++)
        {
            var1 = gPokedexEntries[species].description[page * DEX_TEXT_CHARS_PER_PAGE + i * POKEMON_DEX_LINE_LENGTH + j] & ~0xF;
            var2 = gPokedexEntries[species].description[page * DEX_TEXT_CHARS_PER_PAGE + i * POKEMON_DEX_LINE_LENGTH + j] & 0xF;
            if (var2 == 0)
                var2 = 4;

            DmaCopy16(3, &gPokedexTextGlyphs_Gfx[var1], gGlyphUpperRowBuffer, 0x20);
            DmaCopy16(3, &gPokedexTextGlyphs_Gfx[0x400 + var1], gGlyphLowerRowBuffer, 0x20);
            BlitGlyphToTileBuffer(var2, var0, i);
            var0 += var2;
        }

        var0 = 0;
    }

    DmaCopy16(3, gTempGfxBuffer, BG_VRAM_ADDR_POKEDEX_ENTRY_TEXT, SIZE_OF_VRAM_POKEDEX_ENTRY_TEXT);
}

void BlitGlyphToTileBuffer(s32 arg0, s32 arg1, s32 arg2) {
    int i;
    s32 temp_r2;

    temp_r2 = ((arg1 / 8) * 0x10) + (arg2 * 0x400);

    switch (arg0) {
    case 6:
        switch (arg1 % 8) {
        case 0:
            for(i = 0; i < 8; i++)
            {
                gTempGfxBuffer[i * 2 + temp_r2] |= gGlyphUpperRowBuffer[i * 2];
                gTempGfxBuffer[(i * 2 + 1) + temp_r2] |= gGlyphUpperRowBuffer[i * 2 + 1] & 0xFF;
                gTempGfxBuffer[i * 2 + temp_r2 + 0x200] |= gGlyphLowerRowBuffer[i * 2];
                gTempGfxBuffer[(i * 2 + 1) + temp_r2 + 0x200] |= gGlyphLowerRowBuffer[i * 2 + 1] & 0xFF;
            }
            return;
        case 1:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[i * 2 + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFFF) << 4;
                gTempGfxBuffer[(i * 2 + 1) + temp_r2] |= ((gGlyphUpperRowBuffer[i * 2] & 0xF000) >> 0xC) | ((gGlyphUpperRowBuffer[i * 2 + 1] & 0xFF) << 4);
                gTempGfxBuffer[i * 2 + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFFF) << 4;
                gTempGfxBuffer[(i * 2 + 1) + temp_r2 + 0x200] |= ((gGlyphLowerRowBuffer[i * 2] & 0xF000) >> 0xC) | ((gGlyphLowerRowBuffer[i * 2 + 1] & 0xFF) << 4);
            }
            return;
        case 2:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[i * 2 + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFF) << 8;
                gTempGfxBuffer[(i * 2 + 1) + temp_r2] |= ((gGlyphUpperRowBuffer[i * 2] & 0xFF00) >> 8) | (gGlyphUpperRowBuffer[i * 2 + 1] & 0xFF) << 8;
                gTempGfxBuffer[i * 2 + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFF) << 8;
                gTempGfxBuffer[(i * 2 + 1) + temp_r2 + 0x200] |= ((gGlyphLowerRowBuffer[i * 2] & 0xFF00) >> 8) | (gGlyphLowerRowBuffer[i * 2 + 1] & 0xFF) << 8;
            }
            return;
        case 3:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xF) << 0xC;
                gTempGfxBuffer[(i * 2 + 1) + temp_r2] |= ((gGlyphUpperRowBuffer[i * 2] & 0xFFF0) >> 0x4) | ((gGlyphUpperRowBuffer[i * 2 + 1] & 0xF) << 0xC);
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2 + 1] & 0xF0) >> 0x4;

                gTempGfxBuffer[(i * 2) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xF) << 0xC;
                gTempGfxBuffer[(i * 2 + 1) + temp_r2 + 0x200] |= ((gGlyphLowerRowBuffer[i * 2] & 0xFFF0) >> 0x4) | ((gGlyphLowerRowBuffer[i * 2 + 1] & 0xF) << 0xC);
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2 + 1] & 0xF0) >> 0x4;
            }
            return;
        case 4:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2 + 1) + temp_r2] |= gGlyphUpperRowBuffer[i * 2];
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2] |= gGlyphUpperRowBuffer[i * 2 + 1] & 0xFF;
                gTempGfxBuffer[(i * 2 + 1) + temp_r2 + 0x200] |= gGlyphLowerRowBuffer[i * 2];
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2 + 0x200] |= gGlyphLowerRowBuffer[i * 2 + 1] & 0xFF;
            }
            return;
        case 5:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2 + 1) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFFF) << 4;
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2] |= ((gGlyphUpperRowBuffer[i * 2] & 0xF000) >> 0xC) | ((gGlyphUpperRowBuffer[i * 2 + 1] & 0xFF) << 4);
                gTempGfxBuffer[(i * 2 + 1) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFFF) << 4;
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2 + 0x200] |= ((gGlyphLowerRowBuffer[i * 2] & 0xF000) >> 0xC) | ((gGlyphLowerRowBuffer[i * 2 + 1] & 0xFF) << 4);

            }
            return;
        case 6:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2 + 1) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFF) << 8;
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2] |= ((gGlyphUpperRowBuffer[i * 2] & 0xFF00) >> 0x8) | ((gGlyphUpperRowBuffer[i * 2 + 1] & 0xFF) << 8);
                gTempGfxBuffer[(i * 2 + 1) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFF) << 8;
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2 + 0x200] |= ((gGlyphLowerRowBuffer[i * 2] & 0xFF00) >> 0x8) | ((gGlyphLowerRowBuffer[i * 2 + 1] & 0xFF) << 8);
            }
            return;
        case 7:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2 + 1) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xF) << 0xC;
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2] |= ((gGlyphUpperRowBuffer[i * 2] & 0xFFF0) >> 0x4) | ((gGlyphUpperRowBuffer[i * 2 + 1] & 0xF) << 0xC);
                gTempGfxBuffer[(i * 2 + 0x11) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2 + 1] & 0xF0) >> 4;
                gTempGfxBuffer[(i * 2 + 1) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xF) << 0xC;
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2 + 0x200] |= ((gGlyphLowerRowBuffer[i * 2] & 0xFFF0) >> 0x4) | ((gGlyphLowerRowBuffer[i * 2 + 1] & 0xF) << 0xC);
                gTempGfxBuffer[(i * 2 + 0x11) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2 + 1] & 0xF0) >> 4;

            }
            return;
        default:
            return;
        }
        break;
    case 5:
        switch (arg1 % 8) {
        case 0:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[i * 2 + temp_r2] |= gGlyphUpperRowBuffer[i * 2];
                gTempGfxBuffer[(i * 2 + 1) + temp_r2] |= gGlyphUpperRowBuffer[i * 2 + 1] & 0xF;
                gTempGfxBuffer[i * 2 + temp_r2 + 0x200] |= gGlyphLowerRowBuffer[i * 2];
                gTempGfxBuffer[(i * 2 + 1) + temp_r2 + 0x200] |= gGlyphLowerRowBuffer[i * 2 + 1] & 0xF;

            }
            return;
        case 1:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[i * 2 + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFFF) << 4;
                gTempGfxBuffer[(i * 2 + 1) + temp_r2] |= ((gGlyphUpperRowBuffer[i * 2] & 0xF000) >> 0xC) | ((gGlyphUpperRowBuffer[i * 2 + 1] & 0xF) << 4);
                gTempGfxBuffer[i * 2 + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFFF) << 4;
                gTempGfxBuffer[(i * 2 + 1) + temp_r2 + 0x200] |= ((gGlyphLowerRowBuffer[i * 2] & 0xF000) >> 0xC) | ((gGlyphLowerRowBuffer[i * 2 + 1] & 0xF) << 4);

            }
            return;
        case 2:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[i * 2 + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFF) << 8;
                gTempGfxBuffer[(i * 2 + 1) + temp_r2] |= ((gGlyphUpperRowBuffer[i * 2] & 0xFF00) >> 0x8) | ((gGlyphUpperRowBuffer[i * 2 + 1] & 0xF) << 8);
                gTempGfxBuffer[i * 2 + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFF) << 8;
                gTempGfxBuffer[(i * 2 + 1) + temp_r2 + 0x200] |= ((gGlyphLowerRowBuffer[i * 2] & 0xFF00) >> 0x8) | ((gGlyphLowerRowBuffer[i * 2 + 1] & 0xF) << 8);

            }
            return;
        case 3:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[i * 2 + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xF) << 0xC;
                gTempGfxBuffer[(i * 2 + 1) + temp_r2] |= ((gGlyphUpperRowBuffer[i * 2] & 0xFFF0) >> 0x4) | ((gGlyphUpperRowBuffer[i * 2 + 1] & 0xF) << 0xC);
                gTempGfxBuffer[i * 2 + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xF) << 0xC;
                gTempGfxBuffer[(i * 2 + 1) + temp_r2 + 0x200] |= ((gGlyphLowerRowBuffer[i * 2] & 0xFFF0) >> 0x4) | ((gGlyphLowerRowBuffer[i * 2 + 1] & 0xF) << 0xC);

            }
            return;
        case 4:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2 + 1) + temp_r2] |= gGlyphUpperRowBuffer[i * 2];
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2] |= gGlyphUpperRowBuffer[i * 2 + 1] & 0xF;
                gTempGfxBuffer[(i * 2 + 1) + temp_r2 + 0x200] |= gGlyphLowerRowBuffer[i * 2];
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2 + 0x200] |= gGlyphLowerRowBuffer[i * 2 + 1] & 0xF;

            }
            return;
        case 5:
           for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2 + 0x1) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFFF) << 4;
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2] |= ((gGlyphUpperRowBuffer[i * 2] & 0xF000) >> 0xC) | ((gGlyphUpperRowBuffer[i * 2 + 1] & 0xF) << 4);
                gTempGfxBuffer[(i * 2 + 0x1) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFFF) << 4;
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2 + 0x200] |= ((gGlyphLowerRowBuffer[i * 2] & 0xF000) >> 0xC) | ((gGlyphLowerRowBuffer[i * 2 + 1] & 0xF) << 4);

            }
            return;
        case 6:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2 + 0x1) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFF) << 8;
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2] |= ((gGlyphUpperRowBuffer[i * 2] & 0xFF00) >> 8) | ((gGlyphUpperRowBuffer[i * 2 + 1] & 0xF) << 8);
                gTempGfxBuffer[(i * 2 + 0x1) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFF) << 8;
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2 + 0x200] |= ((gGlyphLowerRowBuffer[i * 2] & 0xFF00) >> 0x8) | ((gGlyphLowerRowBuffer[i * 2 + 1] & 0xF) << 8);

            }
            return;
        case 7:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2 + 1) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xF) << 0xC;
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2] |= ((gGlyphUpperRowBuffer[i * 2] & 0xFFF0) >> 4) | ((gGlyphUpperRowBuffer[i * 2 + 1] & 0xF) << 0xC);
                gTempGfxBuffer[(i * 2 + 1) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xF) << 0xC;
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2 + 0x200] |= ((gGlyphLowerRowBuffer[i * 2] & 0xFFF0) >> 4) | ((gGlyphLowerRowBuffer[i * 2 + 1] & 0xF) << 0xC);

            }
            return;
        }
        break;
    case 4:
        switch (arg1 % 8) {
        case 0:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2) + temp_r2] |= gGlyphUpperRowBuffer[i * 2];
                gTempGfxBuffer[(i * 2) + temp_r2 + 0x200] |= gGlyphLowerRowBuffer[i * 2];
            }
            return;
        case 1:
           for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFFF) << 4;
                gTempGfxBuffer[(i * 2 + 0x1) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xF000) >> 0xC;
                gTempGfxBuffer[(i * 2) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFFF) << 4;
                gTempGfxBuffer[(i * 2 + 0x1) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xF000) >> 0xC;
            }
            return;
        case 2:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFF) << 8;
                gTempGfxBuffer[(i * 2 + 0x1) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFF00) >> 8;
                gTempGfxBuffer[(i * 2) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFF) << 8;
                gTempGfxBuffer[(i * 2 + 0x1) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFF00) >> 8;
            }
            return;
        case 3:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xF) << 0xC;
                gTempGfxBuffer[(i * 2 + 0x1) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFFF0) >> 4;
                gTempGfxBuffer[(i * 2) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xF) << 0xC;
                gTempGfxBuffer[(i * 2 + 0x1) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFFF0) >> 4;
            }
            return;
        case 4:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2 + 1) + temp_r2] |= gGlyphUpperRowBuffer[i * 2];
                gTempGfxBuffer[(i * 2 + 1) + temp_r2 + 0x200] |= gGlyphLowerRowBuffer[i * 2];
            }
            return;
        case 5:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2 + 0x1) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFFF) << 4;
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xF000) >> 0xC;
                gTempGfxBuffer[(i * 2 + 0x1) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFFF) << 4;
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xF000) >> 0xC;
            }
            return;
        case 6:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2 + 0x1) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFF) << 8;
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFF00) >> 8;
                gTempGfxBuffer[(i * 2 + 0x1) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFF) << 8;
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFF00) >> 8;
            }
            return;
        case 7:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2 + 0x1) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xF) << 0xC;
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFFF0) >> 4;
                gTempGfxBuffer[(i * 2 + 0x1) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xF) << 0xC;
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFFF0) >> 4;
            }
            return;
        }
        break;
    case 3:
        switch (arg1 % 8) {
        case 0:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFFF);
                gTempGfxBuffer[(i * 2) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFFF);
            }
            return;
        case 1:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFFF) << 4;
                gTempGfxBuffer[(i * 2) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFFF) << 4;
            }
            return;
        case 2:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFF) << 8;
                gTempGfxBuffer[(i * 2 + 0x1) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xF00) >> 8;
                gTempGfxBuffer[(i * 2) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFF) << 8;
                gTempGfxBuffer[(i * 2 + 0x1) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xF00) >> 8;
            }
            return;
        case 3:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xF) << 0xC;
                gTempGfxBuffer[(i * 2 + 0x1) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFF0) >> 4;
                gTempGfxBuffer[(i * 2) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xF) << 0xC;
                gTempGfxBuffer[(i * 2 + 0x1) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFF0) >> 4;
            }
            return;
        case 4:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2 + 1) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFFF);
                gTempGfxBuffer[(i * 2 + 1) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFFF);
            }
            return;
        case 5:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2 + 1) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFFF) << 4;
                gTempGfxBuffer[(i * 2 + 1) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFFF) << 4;
            }
            return;
        case 6:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2 + 1) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFF) << 8;
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xF00) >> 8;
                gTempGfxBuffer[(i * 2 + 1) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFF) << 8;
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xF00) >> 8;
            }
            return;
        case 7:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2 + 1) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xF) << 0xC;
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFF0) >> 4;
                gTempGfxBuffer[(i * 2 + 1) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xF) << 0xC;
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFF0) >> 4;
            }
            return;
        }
        break;
    case 2:
        switch (arg1 % 8) {
        case 0:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[i * 2 + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFF);
                gTempGfxBuffer[i * 2 + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFF);
            }
            return;
        case 1:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[i * 2 + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFF) << 4;
                gTempGfxBuffer[i * 2 + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFF) << 4;
            }
            return;
        case 2:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[i * 2 + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFF) << 8;
                gTempGfxBuffer[i * 2 + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFF) << 8;
            }
            return;
        case 3:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[i * 2 + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xF) << 0xC;
                gTempGfxBuffer[(i * 2 + 1) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xF0) >> 4;
                gTempGfxBuffer[i * 2 + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xF) << 0xC;
                gTempGfxBuffer[(i * 2 + 1) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xF0) >> 4;
            }
            return;
        case 4:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2 + 1) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFF);
                gTempGfxBuffer[(i * 2 + 1) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFF);
            }
            return;
        case 5:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2 + 1) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFF) << 0x4;
                gTempGfxBuffer[(i * 2 + 1) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFF) << 0x4;
            }
            return;
        case 6:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2 + 1) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xFF) << 0x8;
                gTempGfxBuffer[(i * 2 + 1) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xFF) << 0x8;
            }
            return;
        case 7:
            for(i = 0; i <=7; i++)
            {
                gTempGfxBuffer[(i * 2 + 1) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xF) << 0xC;
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2] |= (gGlyphUpperRowBuffer[i * 2] & 0xF0) >> 4;
                gTempGfxBuffer[(i * 2 + 1) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xF) << 0xC;
                gTempGfxBuffer[(i * 2 + 0x10) + temp_r2 + 0x200] |= (gGlyphLowerRowBuffer[i * 2] & 0xF0) >> 4;
            }
            break;
        }
        break;
    }
}


void LoadPokedexFlagsFromSave(void)
{
    int i;

    for (i = 0; i < NUM_SPECIES; i++)
        gPokedexFlags[i] = gMain_saveData.pokedexFlags[i];

	// It's unclear what these trailing 20 entries are...
    for (i = NUM_SPECIES; i < NUM_SPECIES + 20; i++)
        gPokedexFlags[i] = SPECIES_DEX_UNSEEN;

    gPokedexNumOwned = 0;
    gPokedexNumSeen = 0;
    for (i = 0; i < NUM_SPECIES; i++)
    {
        switch (gPokedexFlags[i])
        {
        case SPECIES_DEX_CAUGHT:
            gPokedexNumOwned++;
        case SPECIES_DEX_SEEN:
        case SPECIES_DEX_SHARED_AND_SEEN:
            gPokedexNumSeen++;
            break;
        case SPECIES_DEX_UNSEEN:
        case SPECIES_DEX_SHARED:
            break;
        }
    }
}

void LoadMonAnimationSprite(s16 species)
{
    int var0;
    s16 quotient;
    s16 remainder;
    s16 var1;

    var0 = gDexAnimationIx[species];
    //No board graphic
    if (var0 == -1)
        return;

    if (var0 < 100)
    {
        //Catch Mon anim section
        gPokedexSpriteCategory = 0;
        quotient = var0 / 5;
        remainder = var0 % 5;

        CopyBgTilesRect(gCatchSpriteGfxPtrs[quotient] + remainder * 0xD80, (void*) OBJ_VRAM_ADDR_POKEDEX_MON_BOARD_ANIM_RECT, 108, 1);
        DmaCopy16(3, gCatchMonPaletteGroups[quotient][remainder], OBJ_PLTT_SLOT(PAL_IX_2), PLTT_SLOT_SIZE);
    }
    else
    {
        //Hatch Mon anim section
        gPokedexSpriteCategory = 1;
        quotient = (var0 - 100) / 6;
        remainder = (var0 - 100) % 6;

        CopyBgTilesRect(gMonHatchSpriteGroupGfx[quotient][remainder], (void *) OBJ_VRAM_ADDR_POKEDEX_MON_HATCH_ANIM_RECT, 135, 1);
        DmaCopy16(3, gMonHatchSpriteGroupPals[quotient][remainder], OBJ_PLTT_SLOT(PAL_IX_3), PLTT_SLOT_SIZE);
    }
}

s16 CheckMonHasAnimation(s16 species)
{
    if (gPokedexFlags[species] == 4 && gDexAnimationIx[species] != -1)
        gPokedexShowAnimSprite = TRUE;
    else
        gPokedexShowAnimSprite = FALSE;

    return gPokedexShowAnimSprite;
}

void ResetPokedex(void)
{
    int i;

    for (i = 0; i < NUM_SPECIES; i++)
        gMain_saveData.pokedexFlags[i] = SPECIES_DEX_UNSEEN;
}

#ifdef JVPPBPORT
uint32_t gPortDexPages[PORT_DEX_MAX_PAGES][PORT_DEX_PAGE_W * PORT_DEX_PAGE_H];
uint32_t gPortDexBack[240 * PORT_DEX_PAGE_H];
uint32_t gPortDexBar[PORT_DEX_BAR_W * PORT_DEX_BAR_H];
uint32_t gPortDexOpenTotals[(240 - PORT_DEX_PAGE_W) * PORT_DEX_PAGE_H];
uint32_t gPortDexDrawer[240 * PORT_DEX_DRAWER_MAX_H];
uint32_t gPortDexDrawerEmpty[240 * PORT_DEX_DRAWER_MAX_H];
int gPortDexDrawerH = PORT_DEX_TEXT_H + PORT_DEX_DRAWER_END;

// Draws each further page of list rows where rows 1-5 go (numbers and balls
// in the BG1 map, names in their tiles), keeps a picture of its text (BG1
// only, see-through elsewhere), and puts everything back. The list's
// backdrop (BG3: stripes, watermark, frame, scroll bar) is kept once.
static void PortCaptureSecondPage(int backdrop)
{
    static u16 tilemap[0x400], vramMap[0x400];
    static u8 nameTiles[0x4000];
    s16 position = gPokedexListPosition;
    int page, x, y;

    if (!gPortStaticView)
        return;
    if (backdrop)
    {
        PortCaptureBgLayers(80, PORT_DEX_PAGE_H, 240, gPortDexBack, 1 << 3);
        for (y = 0; y < PORT_DEX_BAR_H; y++)
            for (x = 0; x < PORT_DEX_BAR_W; x++)
                gPortDexBar[y * PORT_DEX_BAR_W + x] = gPortDexBack[(PORT_DEX_BAR_TOP + y) * 240 + PORT_DEX_BAR_X + x];
        // The backdrop sits at the screen's bottom, below the long bar's
        // end: its own bar is painted over with the stripes beside it.
        for (y = 0; y < PORT_DEX_PAGE_H; y++)
            for (x = PORT_DEX_BAR_X - 1; x < PORT_DEX_BAR_X + PORT_DEX_BAR_W + 1; x++)
                gPortDexBack[y * 240 + x] = gPortDexBack[y * 240 + PORT_DEX_BAR_X + PORT_DEX_BAR_W + 3];
    }
    memcpy(tilemap, gBG0TilemapBuffer, sizeof(tilemap));
    memcpy(vramMap, (void *)BG_VRAM_ADDR_POKEDEX_BG1_TILEMAP, sizeof(vramMap));
    memcpy(nameTiles, (void *)BG_VRAM_ADDR_POKEDEX_TEXT_TILES, sizeof(nameTiles));

    for (page = 1; page * ENTRIES_SHOWN_COUNT < LIST_ROWS && page < PORT_DEX_MAX_PAGES; page++)
    {
        gPokedexListPosition = position + page * ENTRIES_SHOWN_COUNT;
        PrintDexNumbersFromListPosition(gPokedexListPosition);
        PrintCaughtBallFromListPosition(gPokedexListPosition);
        memcpy((void *)BG_VRAM_ADDR_POKEDEX_BG1_TILEMAP, gBG0TilemapBuffer, sizeof(vramMap));
        PortCaptureBgLayers(80, PORT_DEX_PAGE_H, PORT_DEX_PAGE_W, gPortDexPages[page], 1 << 1);
    }

    gPokedexListPosition = position;
    memcpy(gBG0TilemapBuffer, tilemap, sizeof(tilemap));
    memcpy((void *)BG_VRAM_ADDR_POKEDEX_BG1_TILEMAP, vramMap, sizeof(vramMap));
    memcpy((void *)BG_VRAM_ADDR_POKEDEX_TEXT_TILES, nameTiles, sizeof(nameTiles));
}

// The mouse drags the list or its scroll bar (JVPPBPort/src/mouse.c): the
// list from `position` with `selected` chosen (both kept in range, the
// selection on a shown row), refreshed as after a scroll. Silent, like
// pointing at rows.
void PortDexScrollTo(int position, int selected)
{
    if (selected > gPokedexListEntryCount - 1)
        selected = gPokedexListEntryCount - 1;
    if (selected < 0)
        selected = 0;
    if (selected >= position + LIST_ROWS)
        position = selected - LIST_ROWS + 1;
    if (selected < position)
        position = selected;
    if (position > LAST_LIST_POSITION)
        position = LAST_LIST_POSITION;
    if (position < 0)
        position = 0;
    if (position == gPokedexListPosition && selected == gPokedexSelectedMon)
        return;
    gPokedexListPosition = position;
    gPokedexSelectedMon = selected;
    gPokedexCursorOffset = selected - position;
    gPokedexSpriteAnimTimer = 0;
    gPokedexSpriteAnimFrame = 0;
    RefreshPokedexListDisplay();
    gMain.subState = POKEDEX_STATE_2;
}

// When the description drawer starts to open: pictures of the list's text
// as it is (the game erases the rows under the drawer and puts them back as
// it slides; layouts.c keeps showing the list, and Seen/Own at the list's
// bottom, from these), and of the empty drawer, made by putting its rows in
// for a moment as the slide ends with them (BG1: empty text rows, BG2: the
// drawer's middle and last rows), with room for every page of text.
static void PortCaptureDrawer(void)
{
    static u16 bg1[0x400], bg2[0x400];
    static uint32_t page[240 * PORT_DEX_PAGE_H], drawer[240 * (PORT_DEX_TEXT_H + PORT_DEX_DRAWER_END)];
    u16 *bg1Map = (u16 *)BG_VRAM_ADDR_POKEDEX_BG1_TILEMAP, *bg2Map = (u16 *)BG_VRAM_ADDR_POKEDEX_BG2_TILEMAP;
    int pages = gPokedexSelectedMon < BONUS_SPECIES_START ? DEX_TEXT_PAGES : 1;
    int x, y, i;

    if (!gPortStaticView)
        return;
    PortCaptureBgLayers(80, PORT_DEX_PAGE_H, 240, page, 1 << 1);
    for (y = 0; y < PORT_DEX_PAGE_H; y++)
    {
        for (x = 0; x < 240; x++)
        {
            if (x < PORT_DEX_PAGE_W)
                gPortDexPages[0][y * PORT_DEX_PAGE_W + x] = page[y * 240 + x];
            else
                gPortDexOpenTotals[y * (240 - PORT_DEX_PAGE_W) + x - PORT_DEX_PAGE_W] = page[y * 240 + x];
        }
    }

    memcpy(bg1, bg1Map, sizeof(bg1));
    memcpy(bg2, bg2Map, sizeof(bg2));
    for (y = 10; y <= 17; y++)
    {
        for (i = 0; i < 0x20; i++)
        {
            bg1Map[0x20 * y + i] = gDexInfoWindowEmptyTextRowTiles[i];
            bg2Map[0x20 * y + i] = y < 17 ? gDexInfoWindowMiddleRowTiles[i] : gDexInfoWindowBottomRowTiles[i];
        }
    }
    PortCaptureBgLayers(80, PORT_DEX_TEXT_H + PORT_DEX_DRAWER_END, 240, drawer, (1 << 1) | (1 << 2));
    memcpy(bg1Map, bg1, sizeof(bg1));
    memcpy(bg2Map, bg2, sizeof(bg2));

    gPortDexDrawerH = pages * PORT_DEX_TEXT_H + PORT_DEX_DRAWER_END;
    for (i = 0; i < pages; i++)
        memcpy(&gPortDexDrawerEmpty[240 * PORT_DEX_TEXT_H * i], drawer, sizeof(drawer[0]) * 240 * PORT_DEX_TEXT_H);
    memcpy(&gPortDexDrawerEmpty[240 * PORT_DEX_TEXT_H * pages], &drawer[240 * PORT_DEX_TEXT_H],
           sizeof(drawer[0]) * 240 * PORT_DEX_DRAWER_END);
    memcpy(gPortDexDrawer, gPortDexDrawerEmpty, sizeof(gPortDexDrawer));
}

// When the drawer is open: a picture of it with each page of text, one
// under the other (the game then shows the first page, as before).
static void PortCaptureDrawerText(void)
{
    int i, pages = (gPortDexDrawerH - PORT_DEX_DRAWER_END) / PORT_DEX_TEXT_H;

    if (!gPortStaticView)
        return;
    for (i = 0; i < pages; i++)
    {
        PrintDexDescription(gPokedexSelectedMon, i);
        PortCaptureBgLayers(80, PORT_DEX_TEXT_H, 240, &gPortDexDrawer[240 * PORT_DEX_TEXT_H * i], (1 << 1) | (1 << 2));
    }
    PrintDexDescription(gPokedexSelectedMon, gPokedexDescriptionPage);
}

// The cursor (when PortDexCursorParked) and the scroll bar's knob go on lines under the
// screen (PORT_DEX_CURSOR_LINE, PORT_DEX_KNOB_LINE); layouts.c draws those
// lines at the cursor's row and along the long scroll bar. While a pop-up
// is open the knob may go into the screen instead (PortDexKnobLine).
static void PortDexPlaceSprites(struct SpriteGroup *cursor, struct SpriteGroup *knob)
{
    struct OamDataSimple *oam;

    if (!gPortStaticView)
        return;
    if (PortDexCursorParked())
    {
        oam = &cursor->oam[0];
        gOamBuffer[oam->oamId].y = oam->yOffset + PORT_DEX_CURSOR_LINE + 4;
    }
    oam = &knob->oam[0];
    gOamBuffer[oam->oamId].y = oam->yOffset + PortDexKnobLine();
}

// List entry `entry`'s portrait (PORT_DEX_PORTRAIT_W x PORT_DEX_PORTRAIT_H,
// see-through where its colour 0 is), coloured as the header shows it
// (LoadMonPortrait): caught in colour, seen as a silhouette, shared
// darkened; NULL while unseen. Made again when its Pokédex mark changes.
const uint32_t *PortDexPortrait(int entry)
{
    static uint32_t pics[ARRAY_COUNT(gPokedexFlags)][PORT_DEX_PORTRAIT_W * PORT_DEX_PORTRAIT_H];
    static s16 made[ARRAY_COUNT(gPokedexFlags)];  // the mark it was made for, plus 1
    u32 argb[16];
    const u16 *pal;
    const u8 *gfx;
    s16 state;
    int i, x, y;

    if (entry < 0 || entry >= gPokedexListEntryCount || entry >= (int)ARRAY_COUNT(gPokedexFlags))
        return NULL;
    state = gPokedexFlags[entry];
    if (state == SPECIES_DEX_UNSEEN)
        return NULL;
    if (made[entry] == state + 1)
        return pics[entry];

    gfx = gMonPortraitGroupGfx[entry / 15] + (entry % 15) * 0x300;
    pal = state == SPECIES_DEX_SEEN ? gMonPortraitGroupPals[0][15] : gMonPortraitGroupPals[entry / 15][entry % 15];
    for (i = 0; i < 16; i++)
    {
        u32 r = pal[i] & 31, g = (pal[i] >> 5) & 31, b = (pal[i] >> 10) & 31;

        if (state == SPECIES_DEX_SHARED || state == SPECIES_DEX_SHARED_AND_SEEN)
        {
            // As DarkenPalette(..., 0xE) does.
            r -= (r * 0xE) >> 5;
            g -= (g * 0xE) >> 5;
            b -= (b * 0xE) >> 5;
        }
        argb[i] = 0xFF000000 | (((r << 3) | (r >> 2)) << 16) | (((g << 3) | (g >> 2)) << 8) | ((b << 3) | (b >> 2));
    }
    // 48x32 pixels in 16x16 blocks of 2x2 tiles, 3 blocks across.
    for (y = 0; y < PORT_DEX_PORTRAIT_H; y++)
    {
        for (x = 0; x < PORT_DEX_PORTRAIT_W; x++)
        {
            int tile = (y / 16 * 3 + x / 16) * 4 + (y / 8 % 2) * 2 + x / 8 % 2;
            u8 pair = gfx[tile * 32 + (y % 8) * 4 + (x % 8) / 2];
            int c = x % 2 ? pair >> 4 : pair & 15;

            pics[entry][y * PORT_DEX_PORTRAIT_W + x] = c ? argb[c] : 0;
        }
    }
    made[entry] = state + 1;
    return pics[entry];
}
#endif

