#ifndef CRASH_JUMP_H
#define CRASH_JUMP_H

#include <stdbool.h>
#include <stdint.h>

#define CRASH_JUMP_WIF1_EID 0x749520F3u
#define CRASH_JUMP_WIJU_EID 0x7495AF73u
#define CRASH_JUMP_WIJS_EID 0x7495BB73u
#define CRASH_JUMP_WITS_EID 0x7496FB73u

typedef enum {
  CRASH_JUMP_CLIP_WIF1,
  CRASH_JUMP_CLIP_WIJU,
  CRASH_JUMP_CLIP_WIJS,
  CRASH_JUMP_CLIP_WITS,
  CRASH_JUMP_CLIP_COUNT,
} CrashJumpClip;

typedef enum {
  CRASH_JUMP_GROUNDED,
  CRASH_JUMP_WALK_TAKEOFF,
  CRASH_JUMP_IDLE_TAKEOFF,
  CRASH_JUMP_TAKEOFF_HOLD,
  CRASH_JUMP_AIR_SELECT,
  CRASH_JUMP_MOVING_AIR,
  CRASH_JUMP_STATIONARY_AIR,
  CRASH_JUMP_LANDING,
} CrashJumpPhase;

typedef struct {
  CrashJumpClip clip;
  CrashJumpPhase phase;
  uint8_t pose;
  uint8_t ticks_remaining;
  uint8_t air_ticks;
  float y;
  float velocity_y;
  int8_t mirror_x;
  bool active;
  bool land_pending;
} CrashJumpController;

void crash_jump_init(CrashJumpController *jump);
bool crash_jump_can_start(const CrashJumpController *jump);
bool crash_jump_is_airborne(const CrashJumpController *jump);
void crash_jump_start(CrashJumpController *jump, bool from_walk, int8_t mirror_x);
void crash_jump_update(
  CrashJumpController *jump,
  bool jump_held,
  bool directional_input
);

#endif
