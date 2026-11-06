#ifndef GUARD_CONSTANTS_COLLISION_H
#define GUARD_CONSTANTS_COLLISION_H

#define ANGLE_UP 0X4000
#define ANGLE_LEFT 0X8000
#define ANGLE_DOWN 0XC000
#define ANGLE_RIGHT 0X0000

#define ANGLE_UP_RANGE_MIN (ANGLE_UP - 0X10)
#define ANGLE_UP_RANGE_MAX (ANGLE_UP + 0X10)

#define ANGLE_UP_RIGHT_BOUNCE (ANGLE_UP - 0X200)
#define ANGLE_UP_LEFT_BOUNCE  (ANGLE_UP + 0X100)
#define ANGLE_UP_RIGHT_SPIN_BOOST 40
#define ANGLE_UP_LEFT_SPIN_BOOST -40

#define SPIN_BOOST_NONE 0

#define COLLISION_ANGLE_MASK 0x0000FFF0
#define COLLISION_TYPE_MASK 0XF

enum BoardCollisionType{
    BOARD_COLLISION_TYPE_NONE = 0,
    BOARD_COLLISION_TYPE_NORMAL = 1, // Inner walls, the flipper, top half of Dusclops, back of whiscash, inside of bumper. Also used for the raw values of most enemies/projectiles (often changed to 6 internally, see below)
    BOARD_COLLISION_TYPE_BUMPERS = 2, // Bumper
    BOARD_COLLISION_TYPE_SLINGSHOT = 3, // Slingshot, catch Mon
    BOARD_COLLISION_TYPE_OUTER_WALL = 4,
    BOARD_COLLISION_TYPE_CONDITIONAL = 5, // Special: Whiscash face, Dusclops lower area

    // Appears ONLY in code; from objects with a colllision type 1 in raw files.
    // One key place that differs in usage is in the 'outer wall avoidance' in PixelWalkCollisionDetection
    // I suspect this was discovered later in development, and used this to distinguish the moving items from normal board geometry.
    BOARD_COLLISION_TYPE_DYNAMIC = 6, // Special: Duskull, Groudon fire pillar/trap, Kecleon while down, Kyogre, Linoone, Spheal
};

enum CollisionBounceBehaviorType {
    COLLISION_BOUNCE_BEHAVIOR_TYPE_NORMAL = 0,
    COLLISION_BOUNCE_BEHAVIOR_TYPE_BUMPER = 1,
    COLLISION_BOUNCE_BEHAVIOR_TYPE_SLINGSHOT = 2,
    COLLISION_BOUNCE_BEHAVIOR_TYPE_OUTER_WALL = 3,

    COLLISION_BOUNCE_BEHAVIOR_TYPE_DYNAMIC = 5
};

enum CollisionResolutionState{
    COLLISION_RESOLUTION_STATE_NONE = 0,
    COLLISION_RESOLUTION_STATE_PIXEL_WALK_CONTINUING = 1,
    COLLISION_RESOLUTION_STATE_PIXEL_WALK_ONE_STEP = 2,
    COLLISION_RESOLUTION_STATE_LEFT_FLIPPER = 3,
    COLLISION_RESOLUTION_STATE_RIGHT_FLIPPER = 4,
    COLLISION_RESOLUTION_STATE_PASSIVE_FLIPPER_COLLISION = 5,
    COLLISION_RESOLUTION_STATE_UNUSED = 6,
    COLLISION_RESOLUTION_STATE_STOP_BALL = 7
};




#endif //GUARD_CONSTANTS_COLLISION_H