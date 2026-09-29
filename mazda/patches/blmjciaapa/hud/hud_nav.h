#ifndef LIBPATCH_BLMJCIAAPA_HUD_NAV_H
#define LIBPATCH_BLMJCIAAPA_HUD_NAV_H

#include <stdint.h>

enum MazdaIcon : uint8_t {
    HUD_BLANK              = 0,
    HUD_STRAIGHT            = 1,
    HUD_LEFT                = 2,
    HUD_RIGHT               = 3,
    HUD_SLIGHT_LEFT         = 4,
    HUD_SLIGHT_RIGHT        = 5,
    HUD_UNDER_BRIDGE        = 6,
    HUD_OFF_RAMP_RIGHT      = 7,
    HUD_DESTINATION         = 8,
    HUD_SHARP_RIGHT         = 9,
    HUD_U_TURN_RIGHT        = 10,
    HUD_SHARP_LEFT          = 11,
    HUD_FLAG                = 12,
    HUD_U_TURN_LEFT         = 13,
    HUD_FORK_RIGHT          = 14,
    HUD_FORK_LEFT           = 15,
    HUD_MERGE_LEFT          = 16,
    HUD_MERGE_RIGHT         = 17,
    HUD_EMPTY               = 18,
    HUD_CROSS_RIGHT         = 20,
    HUD_CROSS_LEFT          = 21,
    HUD_MEDIAN_U_TURN_LEFT  = 22,
    HUD_MEDIAN_U_TURN_RIGHT = 23,
    HUD_CAR                 = 24,
    HUD_NO_CAR              = 25,
    HUD_OFF_RAMP_LEFT       = 30,
    HUD_T_LEFT              = 31,
    HUD_T_RIGHT             = 32,
    HUD_DESTINATION_LEFT    = 33,
    HUD_DESTINATION_RIGHT   = 34,
    HUD_FLAG_LEFT           = 35,
    HUD_FLAG_RIGHT          = 36,
    HUD_ROUNDABOUT_CCW_BASE = 37,
    HUD_ROUNDABOUT_CW_BASE  = 49,
};

constexpr uint8_t kTurnIcons[20][3] = {
    /*  0 TURN_UNKNOWN                  */ {24, 24, 24},
    /*  1 TURN_DEPART                   */ {35, 36, 12},
    /*  2 TURN_NAME_CHANGE              */ {1, 1, 1},
    /*  3 TURN_SLIGHT_TURN              */ {15, 14, 1},
    /*  4 TURN_TURN                     */ {31, 32, 1},
    /*  5 TURN_SHARP_TURN               */ {11, 9, 24},
    /*  6 TURN_U_TURN                   */ {23, 22, 24},
    /*  7 TURN_ON_RAMP                  */ {30, 7, 6},
    /*  8 TURN_OFF_RAMP                 */ {30, 7, 6},
    /*  9 TURN_FORK                     */ {15, 14, 24},
    /* 10 TURN_MERGE                    */ {16, 17, 24},
    /* 11 TURN_ROUNDABOUT_ENTER         */ {24, 24, 24},
    /* 12 TURN_ROUNDABOUT_EXIT          */ {24, 24, 24},
    /* 13 TURN_ROUNDABOUT_ENTER_AND_EXIT*/ {24, 24, 24},
    /* 14 TURN_STRAIGHT                 */ {1, 1, 1},
    /* 15 unassigned in proto           */ {0, 0, 0},
    /* 16 TURN_FERRY_BOAT               */ {24, 24, 24},
    /* 17 TURN_FERRY_TRAIN              */ {24, 24, 24},
    /* 18 unassigned in proto           */ {0, 0, 0},
    /* 19 TURN_DESTINATION              */ {33, 34, 8},
};


inline uint8_t roundabout_icon(int32_t degrees, int32_t side_index_lr)
{
    const bool left = (side_index_lr == 0);
    switch (degrees) {
    case 360: return left ? 49 : 37;
    case 315: return left ? 60 : 48;
    case 300: return left ? 59 : 47;
    case 270: return left ? 58 : 46;
    case 240: return left ? 57 : 45;
    case 225: return left ? 56 : 44;
    case 180: return left ? 55 : 43;
    case 135: return left ? 54 : 42;
    case 120: return left ? 53 : 41;
    case  90: return left ? 52 : 40;
    case  60: return left ? 51 : 39;
    case  45: return left ? 50 : 38;
    case   0: return 24;
    default:  return 24;
    }
}

// === Distance-unit enum translation ===========================
inline uint8_t map_distance_unit(uint32_t android_unit)
{
    switch (android_unit) {
    case 1: return 1;
    case 2: return 3;
    case 3: return 3;
    case 4: return 2;
    case 5: return 2;
    case 6: return 5;
    default: return 0;
    }
}

// === Turn-icon resolution =====================================
inline uint32_t compute_turn_icon(uint32_t turn_event, uint32_t turn_side,
                                  int32_t turn_angle)
{
    if (turn_event == 13 /*TURN_ROUNDABOUT_ENTER_AND_EXIT*/) {
        int32_t side_lr = (turn_side == 1) ? 0 : 1;
        return roundabout_icon(turn_angle, side_lr);
    }
    if (turn_event < 20) {
        int32_t side_idx = static_cast<int32_t>(turn_side) - 1;
        if (side_idx < 0 || side_idx > 2) side_idx = 2;
        return kTurnIcons[turn_event][side_idx];
    }
    return 0;
}

#endif // LIBPATCH_BLMJCIAAPA_HUD_NAV_H
