#ifndef CRASH_WALK_H
#define CRASH_WALK_H

#include <stdbool.h>
#include <stdint.h>

#define CRASH_WALK_CLIP_EID 0x74974573u

typedef struct {
  uint8_t pose;
  int8_t mirror_x;
  bool active;
} CrashWalkController;

void crash_walk_init(CrashWalkController *walk);
void crash_walk_update(CrashWalkController *walk, bool player_moving);

#endif
