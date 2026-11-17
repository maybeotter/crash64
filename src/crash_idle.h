#ifndef CRASH_IDLE_H
#define CRASH_IDLE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  CRASH_CLIP_WIS1,
  CRASH_CLIP_WIS4,
  CRASH_CLIP_WIS5,
  CRASH_CLIP_WIS6,
  CRASH_CLIP_WIFR,
  CRASH_CLIP_WIBL,
} CrashIdleClip;

typedef struct {
  CrashIdleClip clip;
  uint8_t pose;
  uint8_t phase;
  uint8_t sequence_index;
  uint8_t repeat_count;
  uint16_t ticks_remaining;
  uint32_t rng_state;
  float spin_yaw;
  int8_t mirror_x;
  int8_t spin_direction;
  bool suspended;
  bool fruit_active;
  bool purple_palette_active;
} CrashIdleController;

void crash_idle_init(CrashIdleController *idle, uint32_t random_seed);
void crash_idle_update(CrashIdleController *idle, bool player_moving);
void crash_idle_force_rare(CrashIdleController *idle);

#endif
