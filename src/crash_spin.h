#ifndef CRASH_SPIN_H
#define CRASH_SPIN_H

#include <stdbool.h>
#include <stdint.h>

#define CRASH_SPIN_WIGS_EID 0x74955B73u

typedef enum {
  CRASH_SPIN_INACTIVE,
  CRASH_SPIN_GROUND_ACTIVE,
  CRASH_SPIN_GROUND_RECOVERY,
  CRASH_SPIN_AIR_ACTIVE,
} CrashSpinPhase;

typedef struct {
  CrashSpinPhase phase;
  uint8_t pose;
  uint8_t tick_index;
  int8_t mirror_x;
  int32_t clock;
  int32_t spin_stamp;
  float yaw;
  bool hit_active;
  bool rejected_input;
} CrashSpinController;

void crash_spin_init(CrashSpinController *spin);
bool crash_spin_try_start(
  CrashSpinController *spin,
  bool airborne,
  int8_t mirror_x
);
bool crash_spin_jump_to_air(CrashSpinController *spin);
void crash_spin_update(CrashSpinController *spin);
bool crash_spin_uses_spin_mesh(const CrashSpinController *spin);
bool crash_spin_uses_recovery_mesh(const CrashSpinController *spin);

#endif
