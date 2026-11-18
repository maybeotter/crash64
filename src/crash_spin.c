#include "crash_spin.h"

#define ACTIVE_TICKS 12
#define RECOVERY_TICKS 6
#define SPIN_RECHARGE_TICKS 60
#define SPIN_REST_CAP_TICKS 300
#define SPIN_STORED_TICKS 240
#define ANGLE_TO_RADIANS(value) \
  ((float)(value) * (2.0f * 3.14159265358979323846f / 4096.0f))

static const uint8_t GROUND_POSES[ACTIVE_TICKS] = {
  1, 2, 3, 4, 5, 6, 5, 4, 3, 2, 1, 0,
};

static const uint8_t AIR_POSES[ACTIVE_TICKS] = {
  0, 1, 2, 3, 4, 5, 6, 5, 4, 3, 2, 1,
};

void crash_spin_init(CrashSpinController *spin) {
  *spin = (CrashSpinController){
    .mirror_x = 1,
    .spin_stamp = -SPIN_STORED_TICKS,
  };
}

bool crash_spin_try_start(
  CrashSpinController *spin,
  bool airborne,
  int8_t mirror_x
) {
  spin->rejected_input = false;
  if (spin->phase != CRASH_SPIN_INACTIVE) return false;

  int32_t elapsed = spin->clock - spin->spin_stamp;
  if (elapsed < SPIN_RECHARGE_TICKS) {
    spin->rejected_input = true;
    return false;
  }
  if (elapsed >= SPIN_REST_CAP_TICKS) {
    spin->spin_stamp = spin->clock - SPIN_STORED_TICKS;
  } else {
    spin->spin_stamp += SPIN_RECHARGE_TICKS;
  }

  spin->phase = airborne ? CRASH_SPIN_AIR_ACTIVE : CRASH_SPIN_GROUND_ACTIVE;
  spin->pose = airborne ? AIR_POSES[0] : GROUND_POSES[0];
  spin->tick_index = 0;
  spin->mirror_x = mirror_x < 0 ? -1 : 1;
  spin->yaw = 0.0f;
  spin->hit_active = false;
  return true;
}

bool crash_spin_jump_to_air(CrashSpinController *spin) {
  if (spin->phase != CRASH_SPIN_GROUND_ACTIVE) return false;
  spin->phase = CRASH_SPIN_AIR_ACTIVE;
  spin->pose = AIR_POSES[0];
  spin->tick_index = 0;
  spin->yaw = 0.0f;
  spin->hit_active = false;
  return true;
}

void crash_spin_update(CrashSpinController *spin) {
  spin->hit_active = false;

  if (spin->phase == CRASH_SPIN_GROUND_ACTIVE) {
    if (spin->tick_index == ACTIVE_TICKS) {
      spin->phase = CRASH_SPIN_GROUND_RECOVERY;
      spin->tick_index = 1;
      spin->pose = 0;
      spin->yaw = 0.0f;
    } else {
      spin->pose = GROUND_POSES[spin->tick_index++];
      spin->yaw += spin->mirror_x * ANGLE_TO_RADIANS(0x1C7);
      spin->hit_active = true;
    }
  } else if (spin->phase == CRASH_SPIN_AIR_ACTIVE) {
    if (spin->tick_index == ACTIVE_TICKS) {
      spin->phase = CRASH_SPIN_INACTIVE;
      spin->tick_index = 0;
      spin->yaw = 0.0f;
    } else {
      spin->pose = AIR_POSES[spin->tick_index++];
      spin->yaw += spin->mirror_x * ANGLE_TO_RADIANS(0x2AA);
      spin->hit_active = true;
    }
  } else if (spin->phase == CRASH_SPIN_GROUND_RECOVERY) {
    if (spin->tick_index == RECOVERY_TICKS) {
      spin->phase = CRASH_SPIN_INACTIVE;
      spin->tick_index = 0;
    } else {
      spin->pose = spin->tick_index++;
    }
  }

  ++spin->clock;
}

bool crash_spin_uses_spin_mesh(const CrashSpinController *spin) {
  return spin->phase == CRASH_SPIN_GROUND_ACTIVE
    || spin->phase == CRASH_SPIN_AIR_ACTIVE;
}

bool crash_spin_uses_recovery_mesh(const CrashSpinController *spin) {
  return spin->phase == CRASH_SPIN_GROUND_RECOVERY;
}
