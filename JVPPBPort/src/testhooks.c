// JVPPBPort: test-mode helpers that reach into the game's state, for testing
// parts of the game that are slow to reach by playing.
//
// This file reads and writes game structs, so it uses the game's GBA layout
// rule (jvppbport_layout.h) before anything else.
#include "jvppbport_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m4a.h"
#include "global.h"
#include "main.h"
#include "functions.h"
#include "constants/collision.h"
#include "constants/fields.h"
#include "constants/species.h"
#include "constants/board/main_board.h"
#include "jvppbport.h"

#define EVO_SHOP_SIGN_FRAMES 150  // time for the Mart sign to turn into the evolution sign

static void PlaceBall(s16 x, s16 y)
{
    struct BallState *ball = gCurrentPinballGame->ball;

    ball->positionQ0.x = x;
    ball->positionQ0.y = y;
    ball->positionQ1.x = x * 2;
    ball->positionQ1.y = y * 2;
    ball->positionQ8.x = x << 8;
    ball->positionQ8.y = y << 8;
    ball->velocity.x = 0;
    ball->velocity.y = 0;
}

static void DropBallIntoEvoShopHole(void)
{
    gCurrentPinballGame->ballCatchState = TRAP_EVO_SHOP_HOLE;
    if (gMain.selectedField == FIELD_RUBY)
        DispatchRubyCatchModeInit();
    else
        DispatchSapphireCatchModeInit();
    gCurrentPinballGame->collisionResolutionState = COLLISION_RESOLUTION_STATE_STOP_BALL;
}

// JVPPBPORT_TEST_EVO=frame, during a main-board game:
//  - at that frame: give the player a Zigzagoon to evolve and light the EVO
//    arrows, so the Mart turns into the evolution shop;
//  - a little later, once in normal play: drop the ball into the shop hole
//    (then pick the Pokémon with A, e.g. JVPPBPORT_PRESS=...,4800:A,4950:A);
//  - during evolution mode: move the ball onto each evolution item as it
//    appears, then next to the center hole once the trap door opens.
// JVPPBPORT_TEST_EVO_PARTY=n (1-6): give n Pokémon to evolve instead of one
//   (Zigzagoon, Wurmple, Taillow, Seedot, Lotad, Poochyena), for Evolution
//   Select's side arrows.
// JVPPBPORT_TEST_EVO_NODROP=1: don't drop the ball in (and don't move it
//   during evolution mode): the player shoots for the shop themselves, e.g.
//   after a JVPPBPORT_TEST_BGSAVE made a save of it to CONTINUE on a phone.
// JVPPBPORT_TEST_COINS=n: at the same frame, set the coins to n (Mart tests).
static void TestEvolution(u32 frame)
{
    static const u16 party[] = {SPECIES_ZIGZAGOON, SPECIES_WURMPLE, SPECIES_TAILLOW,
                                SPECIES_SEEDOT, SPECIES_LOTAD, SPECIES_POOCHYENA};
    static long start = -2, partySize = 1, noDrop, coins = -1;
    static bool8 droppedIn;
    int i;

    if (start == -2)
    {
        start = getenv("JVPPBPORT_TEST_EVO") ? strtol(getenv("JVPPBPORT_TEST_EVO"), NULL, 10) : -1;
        if (getenv("JVPPBPORT_TEST_EVO_PARTY"))
            partySize = strtol(getenv("JVPPBPORT_TEST_EVO_PARTY"), NULL, 10);
        if (partySize < 1 || partySize > 6)
            partySize = 1;
        noDrop = getenv("JVPPBPORT_TEST_EVO_NODROP") != NULL;
        if (getenv("JVPPBPORT_TEST_COINS"))
            coins = strtol(getenv("JVPPBPORT_TEST_COINS"), NULL, 10);
    }
    if (start < 0 || gMain.mainState != STATE_GAME_MAIN || gMain.selectedField >= FIELD_BONUS_START)
        return;

    if (frame == (u32)start)
    {
        for (i = 0; i < partySize; i++)
            gCurrentPinballGame->evolvablePartySpecies[i] = party[i];
        gCurrentPinballGame->evolvablePartySize = partySize;
        gCurrentPinballGame->evoArrowProgress = 3;
        if (coins >= 0)
            gCurrentPinballGame->coins = coins;
    }
    else if (!noDrop && !droppedIn && frame >= (u32)start + EVO_SHOP_SIGN_FRAMES
             && gMain.modeChangeFlags == MODE_CHANGE_NONE && gCurrentPinballGame->ballCatchState == 0)
    {
        // Only during normal play: e.g. not while a drained ball is being saved.
        DropBallIntoEvoShopHole();
        droppedIn = TRUE;
    }

    // JVPPBPORT_TEST_TRACE=1: print the evolution state once a second.
    if (getenv("JVPPBPORT_TEST_TRACE") && frame % 30 == 0)
        fprintf(stderr, "frame %u: boardState %d sub %d modeFlags %#x catch %d party %d [0]=%d species %d items %d ball (%d,%d) cam %d locked %d\n",
                frame, gCurrentPinballGame->boardState, gCurrentPinballGame->boardSubState, gMain.modeChangeFlags,
                gCurrentPinballGame->ballCatchState, gCurrentPinballGame->evolvablePartySize,
                gCurrentPinballGame->evolvablePartySpecies[0], gCurrentPinballGame->currentSpecies,
                gCurrentPinballGame->evoItemsCaught, gCurrentPinballGame->ball->positionQ0.x,
                gCurrentPinballGame->ball->positionQ0.y, gMain.bgOffsets[3].yOffset,
                gCurrentPinballGame->cameraLocked);

    if (noDrop || gCurrentPinballGame->boardState != MAIN_BOARD_STATE_EVO_MODE || gMain.modeChangeFlags)
        return;

    switch (gCurrentPinballGame->boardSubState)
    {
    case EVOLUTION_SUBSTATE_SPAWN_EVO_ITEM_AND_CHECK_FOR_COLLECTION:
        if (gCurrentPinballGame->evoItemAppearTimer == 0)
        {
            // Items in slots above 5 sit on the upper (ramp) layer.
            gCurrentPinballGame->boardLayerDepth = gCurrentPinballGame->evoItemSlotIndex > 5 ? 2 : 0;
            PlaceBall(gCurrentPinballGame->evoItemPosX + 8, gCurrentPinballGame->evoItemPosY + 8);
        }
        break;
    case EVOLUTION_SUBSTATE_GRAVITY_WELL:
        // Drop the ball beside the hole once; the game's own pull and
        // "falling in" animation must then run undisturbed.
        {
            s16 dx = gCurrentPinballGame->ball->positionQ0.x - 119;
            s16 dy = gCurrentPinballGame->ball->positionQ0.y - 279;
            if (dx * dx + dy * dy > 30 * 30)
            {
                gCurrentPinballGame->boardLayerDepth = 0;
                PlaceBall(119, 265);
            }
        }
        break;
    }
}

// JVPPBPORT_TEST_STATE=frame:mode, during a main-board game: from that frame
// on, at the first moment of normal play, switch the board to that mode
// (MAIN_BOARD_STATE_*: 2 bonus hole, 3 boss hole, 4 catch, 5 hatch, 6 evo,
// 7 travel, 8 Jirachi). JVPPBPORT_TEST_MART=frame: likewise, drop the ball
// into the Mart hole (with nothing to evolve, so the Mart opens).
static void TestBoardState(u32 frame)
{
    static long stateFrame = -2, state, martFrame, loopFrame = -1;

    if (stateFrame == -2)
    {
        const char *p = getenv("JVPPBPORT_TEST_STATE");
        stateFrame = p ? strtol(p, NULL, 10) : -1;
        state = p && strchr(p, ':') ? strtol(strchr(p, ':') + 1, NULL, 10) : -1;
        martFrame = getenv("JVPPBPORT_TEST_MART") ? strtol(getenv("JVPPBPORT_TEST_MART"), NULL, 10) : -1;
        // JVPPBPORT_TEST_CATCHLOOP=frame: from that frame on, start a Catch 'Em
        // mode every time the board is back in normal play (as if the ball
        // had lit the catch arrows and gone in the hole); the game's own
        // picker chooses the Pokémon, and the autopilot below catches it.
        if (getenv("JVPPBPORT_TEST_CATCHLOOP"))
        {
            loopFrame = strtol(getenv("JVPPBPORT_TEST_CATCHLOOP"), NULL, 10);
            state = MAIN_BOARD_STATE_CATCH_EM_MODE;
        }
    }
    if (gMain.mainState != STATE_GAME_MAIN || gMain.selectedField >= FIELD_BONUS_START)
        return;
    // Autopilot for the modes started here: count the bumper hits that
    // reveal the catch tiles, then throw the ball at the Pokémon.
    if (state >= 0 && (stateFrame == -1 || loopFrame >= 0) && gMain.modeChangeFlags == MODE_CHANGE_NONE && frame % 50 == 0
        && !getenv("JVPPBPORT_TEST_POND_HITS"))
    {
        struct PinballGame *game = gCurrentPinballGame;

        if ((game->boardState == MAIN_BOARD_STATE_CATCH_EM_MODE || game->boardState == MAIN_BOARD_STATE_JIRACHI_CATCH_MODE)
            && game->catchTilesBumperAcknowledgedCount < 6)
            game->catchTilesBumperAcknowledgedCount = 6;
        if (game->boardState == MAIN_BOARD_STATE_CATCH_EM_MODE && game->boardSubState == CATCH_EM_SUBSTATE_CATCH_HIT_PHASE
            && game->captureState != MON_CAPTURE_SPECIAL_STATE_CAPTURE_CUTSCENE)
        {
            game->boardLayerDepth = 0;
            PlaceBall(game->jirachiCenterX + 24, game->jirachiCenterY - 6);
            game->ball->velocity.y = 200;
        }
        // Catch loop: when the game opens the bonus/boss hole (GO TO BONUS
        // after some catches), drop the ball beside the center hole so the
        // game goes on its normal way instead of waiting for the bot.
        if (loopFrame >= 0 && game->ballCatchState == 0
            && (game->boardState == MAIN_BOARD_STATE_BONUS_HOLE_ACTIVE
                || game->boardState == MAIN_BOARD_STATE_BOSS_HOLE_ACTIVE))
        {
            game->boardLayerDepth = 0;
            PlaceBall(119, 265);
            game->ball->velocity.y = 60;
        }
        if (game->boardState == MAIN_BOARD_STATE_EGG_HATCH_MODE && game->boardSubState == EGG_HATCH_SUBSTATE_WANDERING)
        {
            game->boardLayerDepth = 0;
            PlaceBall(game->walkMonXPos / 10 + 12, game->walkMonYPos / 10 + 14);
        }
    }
    if (gCurrentPinballGame->boardState != MAIN_BOARD_STATE_DEFAULT
        || gMain.modeChangeFlags != MODE_CHANGE_NONE || gCurrentPinballGame->ballCatchState != 0)
        return;
    if (loopFrame >= 0 && frame >= (u32)loopFrame)
        RequestBoardStateTransition(MAIN_BOARD_STATE_CATCH_EM_MODE);
    else if (stateFrame >= 0 && frame >= (u32)stateFrame)
    {
        RequestBoardStateTransition(state);
        stateFrame = -1;
    }
    else if (martFrame >= 0 && frame >= (u32)martFrame)
    {
        DropBallIntoEvoShopHole();
        martFrame = -1;
    }
}

// JVPPBPORT_SOUNDTEST=1500:0xFFFF,1501:12,...: at a frame, start song N-1 from
// the song table (m4aSongNumStart), play the cry of species N & 0x7FFF for
// 0x8000 and up (PlayCry_Normal), or stop all sound for 0xFFFF. For
// comparing the sound with a GBA emulator that does the same.
static void TestSounds(u32 frame)
{
    const char *p = getenv("JVPPBPORT_SOUNDTEST");

    while (p && *p)
    {
        const char *colon = strchr(p, ':');

        if (colon && strtoul(p, NULL, 10) == frame)
        {
            u32 value = strtoul(colon + 1, NULL, 0);

            if (value == 0xFFFF)
                m4aMPlayAllStop();
            else if (value & 0x8000)
                PlayCry_Normal(value & 0x7FFF, 0);
            else if (value)
                m4aSongNumStart(value - 1);
        }
        p = strchr(p, ',');
        if (p)
            p++;
    }
}

// JVPPBPORT_GAMEDUMP=file: every frame during a pinball game (incl. the
// attract-mode demo), append the frame number, gMain.idleDemoVariant and
// *gCurrentPinballGame converted back to the GBA layout (pointers zeroed).
// gbaref's GBAREF_GAMEDUMP writes the same from the real ROM, and
// JVPPBPort/tools/cmpgame.py finds the first difference.
static void DumpGame(u32 frame)
{
    static FILE *f;
    static bool8 opened;
    u8 gba[0x1410];
    const u8 *pc = (const u8 *)gCurrentPinballGame;

    if (!opened)
    {
        opened = TRUE;
        if (getenv("JVPPBPORT_GAMEDUMP"))
            f = fopen(getenv("JVPPBPORT_GAMEDUMP"), "wb");
    }
    if (!f || (gMain.mainState != STATE_GAME_MAIN && gMain.mainState != STATE_GAME_IDLE))
        return;

    // Inverse of RestoreGameState(2)'s JVPPBPORT copy.
    memset(gba, 0, sizeof(gba));
    memcpy(gba, pc, 0xF4C);
    memcpy(gba + 0xF50, pc + offsetof(struct PinballGame, fillerF50), 0x132C - 0xF50);
    memcpy(gba + 0x1334, pc + offsetof(struct PinballGame, ballStates), 0x1410 - 0x1334);
    fwrite(&frame, 4, 1, f);
    fwrite(&gMain.idleDemoVariant, 2, 1, f);
    fwrite(gba, 1, sizeof(gba), f);
    fflush(f);
}

// JVPPBPORT_TEST_BGSAVE=frame: at that frame, save the game in progress
// the way Android does when the app goes to the background
// (PortSaveGameInProgress); the game itself must carry on unchanged.
static void TestBackgroundSave(u32 frame)
{
    static u32 at;
    static int read;

    if (!read++)
        at = getenv("JVPPBPORT_TEST_BGSAVE") ? strtoul(getenv("JVPPBPORT_TEST_BGSAVE"), NULL, 10) : 0;
    if (at && frame == at)
        fprintf(stderr, "test: background save %s\n", PortSaveGameInProgress() ? "made" : "not possible now");
}

// JVPPBPORT_TEST_DRAIN=frame[:0], during a main-board game: from that frame
// on, at the first moment of normal play, drop the ball into the drain with
// the Ball Saver on (Latios brings it back), or with ":0" off (ball lost).
// ":gameover": the last ball, lost with a 60,000,000 score (high-score
// name entry follows).
static void TestDrain(u32 frame)
{
    static long drainFrame = -2, saver, gameOver;

    if (drainFrame == -2)
    {
        const char *p = getenv("JVPPBPORT_TEST_DRAIN");
        drainFrame = p ? strtol(p, NULL, 10) : -1;
        gameOver = p && strstr(p, ":gameover") != NULL;
        saver = !gameOver && !(p && strchr(p, ':') && strtol(strchr(p, ':') + 1, NULL, 10) == 0);
    }
    if (drainFrame < 0 || frame < (u32)drainFrame || gMain.mainState != STATE_GAME_MAIN
        || gMain.selectedField >= FIELD_BONUS_START || gMain.modeChangeFlags != MODE_CHANGE_NONE
        || gMain.modeChangeDelayTimer != 0)
        return;
    gCurrentPinballGame->saverTimeRemaining = saver ? 600 : 0;
    if (gameOver)
    {
        gCurrentPinballGame->numLives = 0;
        gCurrentPinballGame->scoreLo = 60000000;
    }
    PlaceBall(gCurrentPinballGame->ball->positionQ0.x, gBoardConfig.fieldLayout.ballDrainY);
    drainFrame = -1;
}

// JVPPBPORT_TEST_CHARGE=1, during a main-board game: keep the Pikachu
// charge full (the bolt shows in the score row).
static void TestCharge(void)
{
    if (getenv("JVPPBPORT_TEST_CHARGE") && gMain.mainState == STATE_GAME_MAIN
        && gMain.selectedField < MAIN_FIELD_COUNT)
        gCurrentPinballGame->chargeFillValue = 13;
}

// JVPPBPORT_TEST_POND=frame:state[:frame2], during a Ruby game: at that
// frame, put that pond state (4 = Lotad, 6 = Whiscash...) in the pond; at
// frame2, press the bonus multiplier button (the "01" -> "02" sign).
// JVPPBPORT_TEST_BALL=frame:x:y[:vx:vy[:layer]], Ruby or Sapphire: put the
// ball there (still, or thrown at that velocity, on that board layer).
static void TestBall(u32 frame)
{
    if (gMain.mainState != STATE_GAME_MAIN || gMain.selectedField >= MAIN_FIELD_COUNT)
        return;
    if (getenv("JVPPBPORT_TEST_BALL"))
    {
        static long ballFrame = -2, bx, by, vx, vy, layer;

        if (ballFrame == -2)
            sscanf(getenv("JVPPBPORT_TEST_BALL"), "%ld:%ld:%ld:%ld:%ld:%ld", &ballFrame, &bx, &by, &vx, &vy, &layer);
        if (ballFrame >= 0 && frame >= (u32)ballFrame && gMain.modeChangeFlags == MODE_CHANGE_NONE)
        {
            gCurrentPinballGame->boardLayerDepth = layer;
            PlaceBall(bx, by);
            gCurrentPinballGame->ball->velocity.x = vx;
            gCurrentPinballGame->ball->velocity.y = vy;
            ballFrame = -1;
        }
    }
}

static void TestPond(u32 frame)
{
    static long pondFrame = -2, state, multFrame = -1;

    if (pondFrame == -2)
    {
        const char *p = getenv("JVPPBPORT_TEST_POND");
        pondFrame = p ? strtol(p, NULL, 10) : -1;
        if (p && (p = strchr(p, ':')) != NULL)
        {
            state = strtol(p + 1, NULL, 10);
            if ((p = strchr(p + 1, ':')) != NULL)
                multFrame = strtol(p + 1, NULL, 10);
        }
    }
    if (gMain.mainState != STATE_GAME_MAIN || gMain.selectedField != FIELD_RUBY)
        return;
    if (pondFrame >= 0 && frame >= (u32)pondFrame)
    {
        gCurrentPinballGame->rubyPondState = state;
        pondFrame = -1;
    }
    if (multFrame >= 0 && frame >= (u32)multFrame)
    {
        gCurrentPinballGame->bonusMultTracker++;
        gCurrentPinballGame->rubyPondChangeTimer = 0;  // and the pond button
        gCurrentPinballGame->rubyPondContentsChanging = TRUE;
        multFrame = -1;
    }
    // JVPPBPORT_TEST_POND_HITS=n: from the pond frame on, hit bumper 0 every
    // 60 frames, n times.
    if (getenv("JVPPBPORT_TEST_POND_HITS") && pondFrame == -1 && frame % 60 == 0)
    {
        static long hits = -1;

        if (hits < 0)
            hits = strtol(getenv("JVPPBPORT_TEST_POND_HITS"), NULL, 10);
        if (hits > 0)
        {
            gCurrentPinballGame->bumperHitCountdown = 2;
            gCurrentPinballGame->pondBumperStates[0] = 107;
            hits--;
        }
    }
}

// JVPPBPORT_TEST_FUZZ=seed, during a main-board game: every 45 frames of
// normal play, throw the ball from a random spot at a random speed, to
// explore lots of board states (for bugs that are hard to reach by hand).
// JVPPBPORT_TEST_FUZZ_SPEED=n sets the highest throw speed (default 400);
// JVPPBPORT_TEST_FUZZ_KICK=1 only kicks the ball where it is (no jumps to
// random spots, some of which are off the playfield), for wall tests.
static int sFuzzSpeed = 400;
static bool8 sFuzzKick;
static int sFuzzSinceHeld;

static void TestFuzz(u32 frame)
{
    static long seed = -2;

    if (seed == -2)
    {
        seed = getenv("JVPPBPORT_TEST_FUZZ") ? strtol(getenv("JVPPBPORT_TEST_FUZZ"), NULL, 10) : -1;
        if (getenv("JVPPBPORT_TEST_FUZZ_SPEED"))
            sFuzzSpeed = atoi(getenv("JVPPBPORT_TEST_FUZZ_SPEED"));
        sFuzzKick = getenv("JVPPBPORT_TEST_FUZZ_KICK") != NULL;
    }
    if (seed >= 0 && gMain.mainState == STATE_GAME_MAIN && gMain.selectedField < MAIN_FIELD_COUNT)
    {
        gCurrentPinballGame->numLives = 3;  // play on forever
        // frames since the ball was last held by the board (see the kick below)
        if (gCurrentPinballGame->ballPhysicsState != BALL_PHYSICS_NORMAL || gCurrentPinballGame->ballGrabbed
            || gMain.modeChangeFlags != MODE_CHANGE_NONE || gCurrentPinballGame->ballCatchState != 0)
            sFuzzSinceHeld = 0;
        else if (sFuzzSinceHeld < 1000)
            sFuzzSinceHeld++;
    }
    if (seed < 0 || gMain.mainState != STATE_GAME_MAIN || gMain.selectedField >= MAIN_FIELD_COUNT
        || gMain.modeChangeFlags != MODE_CHANGE_NONE || gCurrentPinballGame->ballCatchState != 0
        || frame % 45 != 0)
        return;
    // A kick only means something while the ball plays normally: a ball the
    // board holds moves without wall checks, so a kick there sends it straight
    // through everything (a test artifact, not a game bug).
    if (sFuzzKick && (gCurrentPinballGame->ballPhysicsState != BALL_PHYSICS_NORMAL
                      || gCurrentPinballGame->ballGrabbed || sFuzzSinceHeld < 90))
        return;
    seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF;
    if (!sFuzzKick)
    {
        gCurrentPinballGame->boardLayerDepth = 0;
        PlaceBall(40 + (seed >> 8) % 160, 60 + (seed >> 16) % 280);
    }
    seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF;
    gCurrentPinballGame->ball->velocity.x = (s16)((seed >> 4) % (2 * sFuzzSpeed)) - sFuzzSpeed;
    gCurrentPinballGame->ball->velocity.y = (s16)((seed >> 14) % (2 * sFuzzSpeed)) - sFuzzSpeed;
}

// JVPPBPORT_TEST_BOT=1, during a main-board game: a simple player for long
// unattended games (lives topped up forever). It flips the flipper on the
// ball's side when the ball comes down near the flippers, and launches new
// balls. Its "keys" reach the game through PortPinballActionHeld.
static int sBotHeld[2], sBotLaunch;

int PortTestBotAction(int action)
{
    if (action == PINBALL_INPUT_LEFT_FLIPPER)
        return sBotHeld[0] > 0;
    if (action == PINBALL_INPUT_RIGHT_FLIPPER || action == PORT_PINBALL_LAUNCH)
        return sBotHeld[1] > 0 || sBotLaunch > 20;
    return FALSE;
}

static void TestBot(u32 frame)
{
    static int on = -1;
    struct BallState *ball;
    int side;

    if (on < 0)
        on = getenv("JVPPBPORT_TEST_BOT") != NULL;
    if (!on || gMain.mainState != STATE_GAME_MAIN)
        return;
    if (gMain.selectedField < MAIN_FIELD_COUNT)
        gCurrentPinballGame->numLives = 3;
    for (side = 0; side < 2; side++)
        if (sBotHeld[side] > -20)  // held while > 0, then 20 frames down (so a cradled ball rolls off)
            sBotHeld[side]--;
    ball = gCurrentPinballGame->ball;
    // In the launcher lane, waiting: hold the launch 20+ frames, then let go.
    if (ball->positionQ0.x > 225 && ball->positionQ0.y > 330 && ball->velocity.y > -20 && ball->velocity.y < 20)
        sBotLaunch = sBotLaunch > 60 ? 0 : sBotLaunch + 1;
    else
        sBotLaunch = 0;
    if (ball->positionQ0.y > gBoardConfig.fieldLayout.flipperOriginY - 24 && ball->positionQ0.x < 225
        && ball->velocity.y > 0 && sBotHeld[0] <= -20 && sBotHeld[1] <= -20 && (frame & 1))
        sBotHeld[ball->positionQ0.x < 128 ? 0 : 1] = 10;
}

// Is this spot (half-pixel board units) inside a fixed wall on the ball's layer?
static bool8 InWall(s32 x, s32 y)
{
    s32 page, tile;
    u8 type;

    if (x < 0 || y < 0 || x >= 512 || y >= 1024)
        return FALSE;
    page = gCurrentPinballGame->boardLayerDepth + (y / 8) / 64;
    tile = gBoardConfig.fieldLayout.collision.tileData[page][((y / 8) % 64) * 64 + x / 8];
    type = gBoardConfig.fieldLayout.collision.typeData[page][tile * 64 + (y % 8) * 8 + x % 8] & COLLISION_TYPE_MASK;
    return type == BOARD_COLLISION_TYPE_NORMAL || type == BOARD_COLLISION_TYPE_OUTER_WALL;
}

// JVPPBPORT_TEST_WALLCHECK=1, during a main-board game: count frames where the
// ball's center ends up inside a fixed wall ("deep" = also 2 px further in every
// direction), i.e. the ball went through or into a wall. Printed every 5000 frames.
static void TestWallCheck(u32 frame)
{
    static int on = -1;
    static u32 checked, inWall, deep, run, longestRun, outside;
    struct BallState *ball;
    s32 x, y;

    if (on < 0)
        on = getenv("JVPPBPORT_TEST_WALLCHECK") != NULL;
    if (!on)
        return;
    if (frame % 5000 == 0)
        printf("wallcheck %u: checked %u in-wall %u deep %u longest-deep-run %u outside %u rescued %u\n",
               frame, checked, inWall, deep, longestRun, outside, gPortEscapedBalls);
    if (gMain.mainState != STATE_GAME_MAIN || gMain.modeChangeFlags != MODE_CHANGE_NONE
        || gCurrentPinballGame->ballCatchState != 0 || gCurrentPinballGame->ballPhysicsState != BALL_PHYSICS_NORMAL)
    {
        run = 0;
        return;
    }
    ball = gCurrentPinballGame->ball;
    x = ball->positionQ1.x;
    y = ball->positionQ1.y;
    checked++;
    if (ball->positionQ0.x < -8 || ball->positionQ0.x > 264 || ball->positionQ0.y < -40 || ball->positionQ0.y > 470)
        outside++;
    if (gMain.selectedField >= MAIN_FIELD_COUNT)
        return;  // bonus stages: only "outside" (their collision maps differ)
    if (!InWall(x, y))
    {
        run = 0;
        return;
    }
    inWall++;
    if (InWall(x - 4, y) && InWall(x + 4, y) && InWall(x, y - 4) && InWall(x, y + 4))
    {
        deep++;
        if (++run > longestRun)
            longestRun = run;
        if (run == 1)
            printf("wallcheck: deep in a wall at frame %u, ball (%d, %d) layer %d\n",
                   frame, ball->positionQ0.x, ball->positionQ0.y, gCurrentPinballGame->boardLayerDepth);
    }
    else
        run = 0;
}

// F12 while playing: save a "bug snapshot" to attach to a bug report, so a glitch
// seen in normal play can be looked at later: bug_<frame>.txt in the data folder
// (the most telling fields, readable) and bug_<frame>.bin (the raw game state:
// gCurrentPinballGame, gMain, gOamBuffer, the port's sprite list, VRAM,
// palettes). platform.c saves bug_<frame>.bmp next to them.
void PortBugSnapshot(u32 frame)
{
    char name[32], path[1100];
    FILE *f;
    struct PinballGame *game = gCurrentPinballGame;
    int i;

    snprintf(name, sizeof(name), "bug_%u.bin", frame);
    if ((f = fopen(PortDataPath(path, sizeof(path), name), "wb")) != NULL)
    {
        u32 sizes[6] = { sizeof(*game), sizeof(gMain), PORT_MAX_SPRITES * sizeof(struct OamData),
                         sizeof(gPortSprites), PORT_VRAM_SIZE, PORT_PLTT_SIZE };
        fwrite(sizes, sizeof(sizes), 1, f);
        fwrite(game, sizes[0], 1, f);
        fwrite(&gMain, sizes[1], 1, f);
        fwrite(gOamBuffer, sizes[2], 1, f);
        fwrite(gPortSprites, sizes[3], 1, f);
        fwrite(gPortVram, sizes[4], 1, f);
        fwrite(gPortPltt, sizes[5], 1, f);
        fclose(f);
    }
    snprintf(name, sizeof(name), "bug_%u.txt", frame);
    if ((f = fopen(PortDataPath(path, sizeof(path), name), "w")) == NULL)
        return;
    fprintf(f, "frame %u mainState %d subState %d field %d modeChangeFlags %#x\n",
            frame, gMain.mainState, gMain.subState, gMain.selectedField, gMain.modeChangeFlags);
    if (gMain.mainState == STATE_GAME_MAIN && gMain.selectedField < MAIN_FIELD_COUNT)
    {
        fprintf(f, "boardState %d boardSubState %d ballCatchState %d cameraYViewport %d\n",
                game->boardState, game->boardSubState, game->ballCatchState, game->cameraYViewport);
        fprintf(f, "pond: state %d bumperStates %d %d %d changing %d changeTimer %d whiscash %d (state %d frame %d)\n",
                game->rubyPondState, game->pondBumperStates[0], game->pondBumperStates[1], game->pondBumperStates[2],
                game->rubyPondContentsChanging, game->rubyPondChangeTimer, game->shouldProcessWhiscash,
                game->whiscashState, game->whiscashFrameIx);
        for (i = 0; i < 3; i++)
            fprintf(f, "pond bumper %d: logic (%d, %d) collision (%d, %d)\n", i,
                    game->rubyBumperLogicPosition[i].x, game->rubyBumperLogicPosition[i].y,
                    game->rubyBumperCollisionPosition[i].x, game->rubyBumperCollisionPosition[i].y);
    }
    fprintf(f, "active sprite groups:");
    for (i = 0; i < (int)(sizeof(gMain.spriteGroups) / sizeof(gMain.spriteGroups[0])); i++)
        if (gMain.spriteGroups[i].active)
            fprintf(f, " %d", i);
    fprintf(f, "\n");
    fclose(f);
}

// JVPPBPORT_TEST_DEXALL=1: every Pokémon caught (in memory; use a test save).
static void TestDexAll(void)
{
    int i;

    if (!getenv("JVPPBPORT_TEST_DEXALL"))
        return;
    for (i = 0; i < NUM_SPECIES; i++)
        gMain_saveData.pokedexFlags[i] = SPECIES_DEX_CAUGHT;
}

// JVPPBPORT_TEST_MAKEDEX=n (use a test save!): once, at frame 600 (the save
// is loaded by then), write a
// Pokédex with species 0..n-1 caught, plus Latios and Latias, and the four
// e-Reader exclusives unseen (so Options' E-READER box can unlock them the
// normal way). Saved to the save file at once.
static void TestMakeDex(u32 frame)
{
    static int done;
    int i, n;

    if (done || frame < 600 || !getenv("JVPPBPORT_TEST_MAKEDEX"))
        return;
    done = 1;
    n = atoi(getenv("JVPPBPORT_TEST_MAKEDEX"));
    for (i = 0; i < NUM_SPECIES; i++)
        gMain_saveData.pokedexFlags[i] = i < n ? SPECIES_DEX_CAUGHT : SPECIES_DEX_UNSEEN;
    gMain_saveData.pokedexFlags[SPECIES_LATIOS] = SPECIES_DEX_CAUGHT;
    gMain_saveData.pokedexFlags[SPECIES_LATIAS] = SPECIES_DEX_CAUGHT;
    for (i = BONUS_SPECIES_START; i < BONUS_SPECIES_START + NUM_BONUS_SPECIES; i++)
        gMain_saveData.pokedexFlags[i] = SPECIES_DEX_UNSEEN;
    SaveFile_WriteToSram();
    fprintf(stderr, "JVPPBPort: test Pokedex written (%d caught + Latios, Latias)\n", n);
}

// JVPPBPORT_TEST_CATCHLOG=1: print every Catch 'Em species pick (right
// after PickSpeciesForCatchEmMode ran) with what decides a rare pick:
// caught species counted at game start, catches this game, the e-Reader
// card flags and forceSpecialMons (both stay 0 in the port), and the
// exclusives' Pokédex flags. When the pick is one of the rare ones, take
// a snapshot once the Pokémon is on the board and again when it's caught.
static void TestCatchLog(u32 frame)
{
    static int on = -1, lastSub = -1, rare, shotOnBoard, startCaught, picks;
    struct PinballGame *game = gCurrentPinballGame;
    int sub, species;

    if (on == -1)
        on = getenv("JVPPBPORT_TEST_CATCHLOG") != NULL;
    if (!on || gMain.mainState != STATE_GAME_MAIN || gMain.selectedField >= FIELD_BONUS_START)
        return;
    sub = game->boardState == MAIN_BOARD_STATE_CATCH_EM_MODE ? game->boardSubState : -1;
    if (sub == lastSub)
    {
        if (rare && game->caughtMonCount > startCaught)
        {
            fprintf(stderr, "JVPPBPort: catchlog frame %u: rare Pokemon %d CAUGHT (dex flag now %d)\n",
                    frame, game->currentSpecies, gMain_saveData.pokedexFlags[game->currentSpecies]);
            PortRequestSnapshot();
            rare = 0;
        }
        return;
    }
    lastSub = sub;
    if (sub == CATCH_EM_SUBSTATE_LOAD_MON_GRAPHICS)
    {
        species = game->currentSpecies;
        rare = species == SPECIES_LATIOS || species == SPECIES_LATIAS || species >= BONUS_SPECIES_START;
        shotOnBoard = 0;
        startCaught = game->caughtMonCount;
        picks++;
        fprintf(stderr, "JVPPBPort: catchlog frame %u pick %d: species %d%s caughtThisGame %d dexCaught %d "
                        "ereader %d %d %d %d %d force %d flags chik %d cynd %d toto %d aero %d latios %d latias %d\n",
                frame, picks, species, rare ? " RARE" : "", game->caughtMonCount, gBoardConfig.caughtSpeciesCount,
                gMain.eReaderBonuses[0], gMain.eReaderBonuses[1], gMain.eReaderBonuses[2],
                gMain.eReaderBonuses[3], gMain.eReaderBonuses[4], game->forceSpecialMons,
                gMain_saveData.pokedexFlags[SPECIES_CHIKORITA], gMain_saveData.pokedexFlags[SPECIES_CYNDAQUIL],
                gMain_saveData.pokedexFlags[SPECIES_TOTODILE], gMain_saveData.pokedexFlags[SPECIES_AERODACTYL],
                gMain_saveData.pokedexFlags[SPECIES_LATIOS], gMain_saveData.pokedexFlags[SPECIES_LATIAS]);
    }
    else if (rare && !shotOnBoard && sub == CATCH_EM_SUBSTATE_CATCH_HIT_PHASE)
    {
        shotOnBoard = 1;
        fprintf(stderr, "JVPPBPort: catchlog frame %u: rare Pokemon %d on the board\n", frame, game->currentSpecies);
        PortRequestSnapshot();
    }
}

// JVPPBPORT_TEST_BONUSCOUNT=n: once a main-board game is in normal play,
// set the number of bonus stages cleared (4 = the next one is Rayquaza).
static void TestBonusCount(void)
{
    static int done;

    if (done || !getenv("JVPPBPORT_TEST_BONUSCOUNT") || gMain.mainState != STATE_GAME_MAIN
        || gMain.selectedField >= FIELD_BONUS_START || gMain.modeChangeFlags != MODE_CHANGE_NONE)
        return;
    gCurrentPinballGame->numCompletedBonusStages = strtol(getenv("JVPPBPORT_TEST_BONUSCOUNT"), NULL, 10);
    done = TRUE;
}

// JVPPBPORT_TEST_MARTOPEN=frame, during a Ruby game in normal play: open
// the Mart's door, as one hit on it does (and with JVPPBPORT_TEST_COINS=n
// set the coins to n).
static void TestMartOpen(u32 frame)
{
    static long at = -2;

    if (at == -2)
        at = getenv("JVPPBPORT_TEST_MARTOPEN") ? strtol(getenv("JVPPBPORT_TEST_MARTOPEN"), NULL, 10) : -1;
    if (at < 0 || frame < (u32)at || gMain.mainState != STATE_GAME_MAIN || gMain.selectedField != FIELD_RUBY
        || gMain.modeChangeFlags != MODE_CHANGE_NONE)
        return;
    gCurrentPinballGame->shopDoorOpenLevel = 3;
    gCurrentPinballGame->shopDoorAnimDelay = 5;
    if (getenv("JVPPBPORT_TEST_COINS") && !getenv("JVPPBPORT_TEST_EVO"))
        gCurrentPinballGame->coins = strtol(getenv("JVPPBPORT_TEST_COINS"), NULL, 10);
    at = -1;
}

// JVPPBPORT_TEST_WHISCASH=frame, during a Ruby game in normal play: change
// the pond now, to Whiscash (as the game does when it forces Whiscash).
static void TestWhiscash(u32 frame)
{
    static long at = -2;

    if (at == -2)
        at = getenv("JVPPBPORT_TEST_WHISCASH") ? strtol(getenv("JVPPBPORT_TEST_WHISCASH"), NULL, 10) : -1;
    if (at < 0 || frame < (u32)at || gMain.mainState != STATE_GAME_MAIN || gMain.selectedField != FIELD_RUBY
        || gMain.modeChangeFlags != MODE_CHANGE_NONE)
        return;
    gCurrentPinballGame->forcePondToWhiscash = TRUE;
    gCurrentPinballGame->rubyPondChangeTimer = 0;
    gCurrentPinballGame->rubyPondContentsChanging = TRUE;
    at = -1;
}

void PortTestHooks(u32 frame)
{
    TestWhiscash(frame);
    TestBonusCount();
    TestMartOpen(frame);
    TestMakeDex(frame);
    TestCatchLog(frame);
    TestDexAll();
    TestFuzz(frame);
    TestBot(frame);
    TestWallCheck(frame);
    TestBall(frame);
    TestPond(frame);
    TestCharge();
    TestDrain(frame);
    DumpGame(frame);
    TestEvolution(frame);
    TestBoardState(frame);
    TestSounds(frame);
    TestBackgroundSave(frame);
}
