#include "crash_walk.h"

#define WALK_POSE_COUNT 10

void crash_walk_init(CrashWalkController *walk) {
  *walk = (CrashWalkController){
    .mirror_x = 1,
  };
}

void crash_walk_update(CrashWalkController *walk, bool player_moving) {
  if (!player_moving) {
    walk->active = false;
    return;
  }
  if (!walk->active) {
    walk->active = true;
    walk->pose = 0;
    walk->mirror_x = 1;
    return;
  }

  ++walk->pose;
  if (walk->pose == WALK_POSE_COUNT) {
    walk->pose = 0;
    walk->mirror_x = -walk->mirror_x;
  }
}
