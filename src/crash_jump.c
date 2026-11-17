#include "crash_jump.h"

#define GAME_TICK_HZ 30.0f
#define TICKS_PER_FRAME 34.0f
#define MAX_GROUND_SPEED_RAW 0x96000
#define JUMP_VELOCITY_RAW 0x106800
#define JUMP_HOLD_ACCEL_RAW 0x154E00
#define GRAVITY_RAW 4000
#define AIR_ANIM_THRESHOLD_RAW 0x7D000

#define RAW_SPEED_TO_WORLD (30.0f / MAX_GROUND_SPEED_RAW)
#define JUMP_VELOCITY (JUMP_VELOCITY_RAW * RAW_SPEED_TO_WORLD)
#define GRAVITY_PER_SECOND \
  (GRAVITY_RAW * TICKS_PER_FRAME * RAW_SPEED_TO_WORLD * GAME_TICK_HZ)
#define HOLD_ACCEL_PER_SECOND \
  (JUMP_HOLD_ACCEL_RAW * TICKS_PER_FRAME / 1024.0f \
   * RAW_SPEED_TO_WORLD * GAME_TICK_HZ)
#define AIR_ANIM_THRESHOLD (AIR_ANIM_THRESHOLD_RAW * RAW_SPEED_TO_WORLD)

static void set_pose(
  CrashJumpController *jump,
  CrashJumpClip clip,
  uint8_t pose,
  uint8_t duration
) {
  jump->clip = clip;
  jump->pose = pose;
  jump->ticks_remaining = duration;
}

static void begin_landing(CrashJumpController *jump) {
  jump->phase = CRASH_JUMP_LANDING;
  jump->land_pending = false;
  jump->y = 0.0f;
  jump->velocity_y = 0.0f;
  set_pose(jump, CRASH_JUMP_CLIP_WITS, 0, 2);
}

static void begin_selected_air(
  CrashJumpController *jump,
  bool directional_input
) {
  if (directional_input) {
    jump->phase = CRASH_JUMP_MOVING_AIR;
    set_pose(jump, CRASH_JUMP_CLIP_WIJU, 0, 1);
  } else {
    jump->phase = CRASH_JUMP_STATIONARY_AIR;
    set_pose(jump, CRASH_JUMP_CLIP_WIJS, 0, 1);
  }
}

static void advance_animation(
  CrashJumpController *jump,
  bool directional_input
) {
  switch (jump->phase) {
    case CRASH_JUMP_WALK_TAKEOFF:
      if (jump->pose < 3) {
        set_pose(jump, CRASH_JUMP_CLIP_WIF1, jump->pose + 1, 1);
      } else {
        jump->phase = CRASH_JUMP_TAKEOFF_HOLD;
        set_pose(jump, CRASH_JUMP_CLIP_WIJU, 0, 1);
      }
      break;

    case CRASH_JUMP_IDLE_TAKEOFF:
    case CRASH_JUMP_TAKEOFF_HOLD:
      jump->phase = CRASH_JUMP_AIR_SELECT;
      break;

    case CRASH_JUMP_MOVING_AIR:
      if (jump->pose < 16) {
        set_pose(jump, CRASH_JUMP_CLIP_WIJU, jump->pose + 1, 1);
      }
      break;

    case CRASH_JUMP_STATIONARY_AIR:
      if (jump->clip == CRASH_JUMP_CLIP_WIJS && jump->pose < 5) {
        set_pose(jump, CRASH_JUMP_CLIP_WIJS, jump->pose + 1, 1);
      } else if (jump->clip == CRASH_JUMP_CLIP_WIJS) {
        set_pose(jump, CRASH_JUMP_CLIP_WIJU, 15, 1);
      }
      break;

    case CRASH_JUMP_LANDING:
      if (jump->pose < 4) {
        set_pose(jump, CRASH_JUMP_CLIP_WITS, jump->pose + 1, 2);
      } else {
        jump->phase = CRASH_JUMP_GROUNDED;
        jump->active = false;
      }
      break;

    case CRASH_JUMP_AIR_SELECT:
      if (jump->velocity_y < AIR_ANIM_THRESHOLD) {
        begin_selected_air(jump, directional_input);
      }
      break;

    default:
      break;
  }
}

void crash_jump_init(CrashJumpController *jump) {
  *jump = (CrashJumpController){
    .phase = CRASH_JUMP_GROUNDED,
    .mirror_x = 1,
  };
}

bool crash_jump_can_start(const CrashJumpController *jump) {
  return !jump->active || jump->phase == CRASH_JUMP_LANDING;
}

bool crash_jump_is_airborne(const CrashJumpController *jump) {
  return jump->active && jump->phase != CRASH_JUMP_LANDING;
}

void crash_jump_start(CrashJumpController *jump, bool from_walk, int8_t mirror_x) {
  *jump = (CrashJumpController){
    .phase = from_walk ? CRASH_JUMP_WALK_TAKEOFF : CRASH_JUMP_IDLE_TAKEOFF,
    .velocity_y = JUMP_VELOCITY,
    .mirror_x = from_walk ? -mirror_x : mirror_x,
    .active = true,
  };
  if (from_walk) {
    set_pose(jump, CRASH_JUMP_CLIP_WIF1, 0, 1);
  } else {
    set_pose(jump, CRASH_JUMP_CLIP_WIJU, 0, 2);
  }
}

void crash_jump_update(
  CrashJumpController *jump,
  bool jump_held,
  bool directional_input
) {
  if (!jump->active) return;
  if (jump->land_pending) begin_landing(jump);
  if (jump->phase == CRASH_JUMP_LANDING && directional_input) {
    jump->phase = CRASH_JUMP_GROUNDED;
    jump->active = false;
    return;
  }

  if (jump->ticks_remaining == 0) {
    advance_animation(jump, directional_input);
  }
  if (!jump->active) return;

  if (crash_jump_is_airborne(jump)) {
    if (jump_held && jump->air_ticks > 0) {
      jump->velocity_y += HOLD_ACCEL_PER_SECOND / GAME_TICK_HZ;
    }
    jump->y += jump->velocity_y / GAME_TICK_HZ;
    jump->velocity_y -= GRAVITY_PER_SECOND / GAME_TICK_HZ;
    ++jump->air_ticks;
    if (jump->y <= 0.0f && jump->velocity_y < 0.0f) {
      jump->y = 0.0f;
      jump->velocity_y = 0.0f;
      jump->land_pending = true;
    }
  }

  if (jump->ticks_remaining > 0) --jump->ticks_remaining;
}
