#include "global.h"
#include "main.h"
#include "constants/bg_music.h"
#include "constants/fields.h"
#include "m4a.h"
#include "constants/board/center_screen_states.h"

extern void UpdateFrameProcess7_DrawBall_MainBoards(void);
extern void UpdateFrameProcess7_DrawBall_BonusBoards(void);
extern void RunMonCaptureSequence(void);

EWRAM_DATA struct PinballGame *gCurrentPinballGame;
EWRAM_DATA struct BoardConfig gBoardConfig;

// Rom_1 data
extern const u16 gGravityDeltas_Strong[4];
extern const u16 gGravityDeltas_Medium[4];
extern const u16 gGravityDeltas_Light[4];

void InitFrameProcess5_BallMovement_AllBoards(void)
{
    s16 i;
    if ( !gMain.isBonusField )
    {
        gCurrentPinballGame->ball = &gCurrentPinballGame->ballStates[0];
        InitBallState(0);
        UpdateFrameProcess7_DrawBall_MainBoards();
    }
    else
    {
        for (i = 0; i < 2; i++)
        {
            gCurrentPinballGame->ball = &gCurrentPinballGame->ballStates[i];
            InitBallState(i);
        }
        UpdateFrameProcess7_DrawBall_BonusBoards();
    }
}

void InitBallState(s16 arg0)
{
    struct BallState *ball = &gCurrentPinballGame->ballStates[arg0];
    ball->positionQ0.x = gBoardConfig.fieldLayout.ballSpawnX;
    ball->positionQ0.y = gBoardConfig.fieldLayout.ballSpawnY;
    ball->positionQ8.x = Q_24_8(ball->positionQ0.x);
    ball->positionQ8.y = Q_24_8(ball->positionQ0.y);
    ball->positionQ1.x = gBoardConfig.fieldLayout.ballSpawnX * 2;
    ball->positionQ1.y = gBoardConfig.fieldLayout.ballSpawnY * 2;
    ball->scale = 0x100;
    ball->velocity.x = 0;
    ball->velocity.y = 0;
    if (gMain.selectedField == FIELD_RUBY)
        ball->oamPriority = 3;
    else
        ball->oamPriority = 1;

    ball->spinSpeed = 0;
    gCurrentPinballGame->ballInLowerHalf = TRUE;
}

// Before updating the ball's position, cap the ball's velocity
// vector to a maximum speed.
#define UPDATE_BALL_POSITION(max_speed, angle)           \
{                                                        \
    maxSpeed = (max_speed);                              \
    if (squaredSpeed > maxSpeed * maxSpeed)              \
    {                                                    \
        velocity.x = MulCos(maxSpeed,angle); \
        velocity.y = MulSin(-maxSpeed,angle); \
    }                                                    \
    else                                                 \
    {                                                    \
        velocity.x = currentBall->velocity.x;                \
        velocity.y = currentBall->velocity.y;                \
    }                                                    \
    currentBall->positionQ8.x += velocity.x;                   \
    currentBall->positionQ8.y += velocity.y;                   \
}

#ifdef JVPPBPORT
// FAST ball speed only (SLOW and NORMAL stay exactly the GBA's): walls in the
// collision maps can be thin enough for a ball faster than the GBA's speeds to
// cross. Out there the game reads past its collision map, which on PC can
// crash. A ball that leaves the board through a side or the top goes back to
// the launcher instead (the bottom is the drain). Legal ball positions are
// about x 18..243, y 31 and down.
u32 gPortEscapedBalls;

static void PortRescueEscapedBall(struct BallState *ball)
{
    if (gCurrentPinballGame->ballSpeed != BALL_SPEED_FAST)
        return;
    // A ball the board is holding can be parked off the table on purpose
    // (Sapphire's Pelipper return puts it at -28,-10): leave those alone.
    if (gCurrentPinballGame->ballPhysicsState != BALL_PHYSICS_NORMAL)
        return;
    if (ball->positionQ8.x >= Q_24_8(4) && ball->positionQ8.x <= Q_24_8(251) && ball->positionQ8.y >= Q_24_8(4))
        return;
    gPortEscapedBalls++;
    PortLog("ball rescued at %d,%d field %d", ball->positionQ8.x >> 8, ball->positionQ8.y >> 8, gMain.selectedField);
    // Like a new ball (InitBallState + the mode change code): at the launcher,
    // and the launch ramp open again. Ruby closes its ramp (collision config 1)
    // once a ball has gone up it; a ball put back with it closed hits it at the
    // overpass on every launch (playtester, 2026-11-11, saved games too).
    ball->positionQ0.x = gBoardConfig.fieldLayout.ballSpawnX;
    ball->positionQ0.y = gBoardConfig.fieldLayout.ballSpawnY;
    ball->positionQ8.x = Q_24_8(gBoardConfig.fieldLayout.ballSpawnX);
    ball->positionQ8.y = Q_24_8(gBoardConfig.fieldLayout.ballSpawnY);
    ball->positionQ1.x = gBoardConfig.fieldLayout.ballSpawnX * 2;
    ball->positionQ1.y = gBoardConfig.fieldLayout.ballSpawnY * 2;
    ball->prevPositionQ8 = ball->positionQ8;
    ball->velocity.x = 0;
    ball->velocity.y = 0;
    ball->spinSpeed = 0;
    ball->oamPriority = gMain.selectedField == FIELD_RUBY ? 3 : 1;
    gCurrentPinballGame->boardLayerDepth = 0;
    gCurrentPinballGame->ballInLowerHalf = TRUE;
    SetBoardCollisionConfig(0);
    gCurrentPinballGame->boardCollisionConfigChanged = FALSE;
}
#endif

void UpdateFrameProcess5_BallMovement_MainBoards(void)
{
    struct Vector16 velocity;
    struct BallState *currentBall;
    int xx, yy;
    int squaredSpeed;
    int maxSpeed;

    currentBall = gCurrentPinballGame->ball;
    currentBall->prevPositionQ8 = currentBall->positionQ8;

    if (gCurrentPinballGame->captureState != MON_CAPTURE_SPECIAL_STATE_CAPTURE_CUTSCENE)
    {
        if (BALL_SPEED_IS_SLOW)
        {
            u16 angle;

            if (gCurrentPinballGame->ballPhysicsState == BALL_PHYSICS_NORMAL)
            {
                // Gravity is applied at different strengths, depending on fast the ball is
                // currently moving downwards.
                if (currentBall->velocity.y > 160)
                    currentBall->velocity.y += gGravityDeltas_Light[gCurrentPinballGame->gravityStrengthIndex];
                else if (currentBall->velocity.y > 80)
                    currentBall->velocity.y += gGravityDeltas_Medium[gCurrentPinballGame->gravityStrengthIndex];
                else
                    currentBall->velocity.y += gGravityDeltas_Strong[gCurrentPinballGame->gravityStrengthIndex];
            }

            angle = ArcTan2(currentBall->velocity.x, -currentBall->velocity.y);
            xx = currentBall->velocity.x * currentBall->velocity.x;
            yy = currentBall->velocity.y * currentBall->velocity.y;
            squaredSpeed = xx + yy;

            if (currentBall->positionQ0.y < 380)
            {
                UPDATE_BALL_POSITION(272, angle);
            }
            else
            {
                UPDATE_BALL_POSITION(400, angle);
            }
        }
        else
        {
            u16 angle;

            if (gCurrentPinballGame->ballPhysicsState == BALL_PHYSICS_NORMAL)
            {
                // Gravity is applied at different strengths, depending on fast the ball is
                // currently moving downwards.
                if (currentBall->velocity.y > NORMAL_OR_FAST(200, 270))
                    currentBall->velocity.y += gGravityDeltas_Light[gCurrentPinballGame->gravityStrengthIndex];
                else if (currentBall->velocity.y > NORMAL_OR_FAST(100, 135))
                    currentBall->velocity.y += gGravityDeltas_Medium[gCurrentPinballGame->gravityStrengthIndex];
                else
                    currentBall->velocity.y += gGravityDeltas_Strong[gCurrentPinballGame->gravityStrengthIndex];
            }

            angle = ArcTan2(currentBall->velocity.x, -currentBall->velocity.y);
            xx = currentBall->velocity.x * currentBall->velocity.x;
            yy = currentBall->velocity.y * currentBall->velocity.y;
            squaredSpeed = xx + yy;

            if (currentBall->positionQ0.y < 380)
            {
                UPDATE_BALL_POSITION(NORMAL_OR_FAST(336, 454), angle);
            }
            else
            {
                UPDATE_BALL_POSITION(NORMAL_OR_FAST(400, 540), angle);
            }
        }
    }
    else
    {
        RunMonCaptureSequence();
    }

#ifdef JVPPBPORT
    PortRescueEscapedBall(currentBall);
#endif
    currentBall->prevPositionQ1 = currentBall->positionQ1;
    currentBall->positionQ1.x = (currentBall->positionQ8.x + 64) / 128;
    currentBall->positionQ1.y = (currentBall->positionQ8.y + 64) / 128;
    currentBall->prevSpinSpeed = currentBall->spinSpeed;
    currentBall->spinAngle += currentBall->spinSpeed;
}

void UpdateFrameProcess5_BallMovement_BonusBoards(void)
{
    struct Vector16 velocity;
    struct BallState *currentBall;
    int xx, yy;
    int squaredSpeed;
    int maxSpeed;

    currentBall = gCurrentPinballGame->ball;
    currentBall->prevPositionQ8 = currentBall->positionQ8;
    if (gCurrentPinballGame->captureState != MON_CAPTURE_SPECIAL_STATE_CAPTURE_CUTSCENE)
    {
        if (gCurrentPinballGame->ballPhysicsState == BALL_PHYSICS_NORMAL
            && !gCurrentPinballGame->ballGrabbed)
        {
            // Gravity is applied at different strengths, depending on fast the ball is
            // currently moving downwards.
            if (currentBall->velocity.y > NORMAL_OR_FAST(150, 203))
                currentBall->velocity.y += gGravityDeltas_Light[gCurrentPinballGame->gravityStrengthIndex];
            else if (currentBall->velocity.y > NORMAL_OR_FAST(75, 101))
                currentBall->velocity.y += gGravityDeltas_Medium[gCurrentPinballGame->gravityStrengthIndex];
            else
                currentBall->velocity.y += gGravityDeltas_Strong[gCurrentPinballGame->gravityStrengthIndex];
        }

        if (BALL_SPEED_IS_SLOW)
        {
            u16 angle;
            angle = ArcTan2(currentBall->velocity.x, -currentBall->velocity.y);
            xx = currentBall->velocity.x * currentBall->velocity.x;
            yy = currentBall->velocity.y * currentBall->velocity.y;
            squaredSpeed = xx + yy;

            if (gMain.selectedField < FIELD_BOSS_START)
            {
                if (currentBall->positionQ0.y < 150)
                {
                    UPDATE_BALL_POSITION(272, angle);
                }
                else
                {
                    UPDATE_BALL_POSITION(400, angle);
                }
            }
            else if (gMain.selectedField == FIELD_SPHEAL)
            {
                if (currentBall->positionQ0.y < 218)
                {
                    UPDATE_BALL_POSITION(272, angle);
                }
                else
                {
                    UPDATE_BALL_POSITION(400, angle);
                }
            }
            else
            {
                if (currentBall->positionQ0.y < 218)
                {
                    UPDATE_BALL_POSITION(272, angle);
                }
                else
                {
                    UPDATE_BALL_POSITION(400, angle);
                }
            }
        }
        else
        {
            u16 angle;
            angle = ArcTan2(currentBall->velocity.x, -currentBall->velocity.y);
            xx = currentBall->velocity.x * currentBall->velocity.x;
            yy = currentBall->velocity.y * currentBall->velocity.y;
            squaredSpeed = xx + yy;

            if (gMain.selectedField < FIELD_BOSS_START)
            {
                if (currentBall->positionQ0.y < 150)
                {
                    UPDATE_BALL_POSITION(NORMAL_OR_FAST(304, 410), angle);
                }
                else
                {
                    UPDATE_BALL_POSITION(NORMAL_OR_FAST(400, 540), angle);
                }
            }
            else if (gMain.selectedField == FIELD_SPHEAL)
            {
                if (currentBall->positionQ0.y < 218)
                {
                    UPDATE_BALL_POSITION(NORMAL_OR_FAST(272, 367), angle);
                }
                else
                {
                    UPDATE_BALL_POSITION(NORMAL_OR_FAST(400, 540), angle);
                }
            }
            else
            {
                if (currentBall->positionQ0.y < 218)
                {
                    UPDATE_BALL_POSITION(NORMAL_OR_FAST(304, 410), angle);
                }
                else
                {
                    UPDATE_BALL_POSITION(NORMAL_OR_FAST(400, 540), angle);
                }
            }
        }
    }
    else
    {
        RunMonCaptureSequence();
    }

#ifdef JVPPBPORT
    PortRescueEscapedBall(currentBall);
#endif
    currentBall->prevPositionQ1 = currentBall->positionQ1;
    currentBall->positionQ1.x = (currentBall->positionQ8.x + 64) / 128;
    currentBall->positionQ1.y = (currentBall->positionQ8.y + 64) / 128;
    currentBall->prevSpinSpeed = currentBall->spinSpeed;
    currentBall->spinAngle += currentBall->spinSpeed;
}
