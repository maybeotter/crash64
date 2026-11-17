#include "crash_idle.h"

#include <libdragon.h>
#include <math.h>

#define ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))
#define PS1_ANGLE_TO_RADIANS(value) ((float)(value) * (2.0f * M_PI / 4096.0f))

typedef struct {
  uint8_t pose;
  uint8_t duration;
  uint8_t event;
} IdleStep;

enum {
  EVENT_NONE,
  EVENT_FRUIT_SPAWN,
  EVENT_CUE,
};

enum {
  PHASE_ORDINARY_INITIAL_HOLD,
  PHASE_ORDINARY_POSE_12,
  PHASE_ORDINARY_BLINK,
  PHASE_ORDINARY_EXTRA_HOLD,
  PHASE_ORDINARY_POSES_1_5,
  PHASE_ORDINARY_HOLD_6,
  PHASE_ORDINARY_POSES_7_9,
  PHASE_ORDINARY_REVERSE,
  PHASE_ORDINARY_POSES_10_11,
  PHASE_RARE_WIS4,
  PHASE_RARE_WIS5,
  PHASE_RARE_WIS6_INTRO,
  PHASE_RARE_WIS6_REPEAT,
  PHASE_RARE_WIS6_TAIL,
  PHASE_RARE_WIFR,
  PHASE_RARE_WIBL,
};

static const IdleStep BLINK_STEPS[] = {
  {12, 3, EVENT_NONE}, {11, 3, EVENT_NONE},
  {12, 3, EVENT_NONE}, {11, 6, EVENT_NONE},
};

static const IdleStep WIS4_STEPS[] = {
  {0, 1, EVENT_NONE}, {1, 1, EVENT_NONE}, {2, 1, EVENT_NONE},
  {3, 1, EVENT_NONE}, {4, 1, EVENT_NONE}, {5, 1, EVENT_NONE},
  {6, 1, EVENT_NONE}, {7, 1, EVENT_NONE}, {8, 1, EVENT_NONE},
  {9, 6, EVENT_NONE}, {10, 1, EVENT_NONE}, {11, 1, EVENT_NONE},
  {12, 1, EVENT_NONE}, {13, 1, EVENT_NONE}, {14, 1, EVENT_NONE},
  {15, 1, EVENT_NONE}, {16, 1, EVENT_NONE}, {17, 1, EVENT_NONE},
  {18, 1, EVENT_NONE}, {19, 1, EVENT_NONE}, {20, 1, EVENT_FRUIT_SPAWN},
  {21, 1, EVENT_NONE}, {22, 1, EVENT_NONE}, {23, 1, EVENT_NONE},
  {24, 1, EVENT_NONE},
};

static const IdleStep WIS5_STEPS[] = {
  {0, 1, EVENT_NONE}, {1, 1, EVENT_NONE}, {2, 1, EVENT_NONE},
  {3, 1, EVENT_NONE}, {4, 30, EVENT_CUE}, {5, 1, EVENT_NONE},
  {6, 1, EVENT_NONE}, {7, 1, EVENT_NONE}, {8, 1, EVENT_NONE},
  {9, 1, EVENT_NONE}, {10, 1, EVENT_NONE}, {11, 1, EVENT_NONE},
  {12, 1, EVENT_NONE}, {13, 1, EVENT_NONE}, {14, 1, EVENT_NONE},
  {15, 1, EVENT_NONE}, {16, 1, EVENT_NONE}, {17, 1, EVENT_NONE},
  {18, 1, EVENT_NONE}, {19, 1, EVENT_NONE}, {20, 1, EVENT_NONE},
  {21, 1, EVENT_NONE}, {22, 1, EVENT_NONE}, {23, 1, EVENT_NONE},
  {24, 45, EVENT_NONE},
};

static const IdleStep WIS6_INTRO_STEPS[] = {
  {0, 1, EVENT_NONE}, {1, 1, EVENT_NONE}, {2, 1, EVENT_NONE},
  {3, 1, EVENT_NONE}, {4, 1, EVENT_NONE}, {5, 1, EVENT_NONE},
  {6, 1, EVENT_NONE}, {7, 1, EVENT_NONE}, {8, 1, EVENT_NONE},
  {9, 30, EVENT_NONE}, {10, 1, EVENT_NONE}, {11, 1, EVENT_NONE},
  {12, 1, EVENT_NONE}, {13, 1, EVENT_NONE},
};

static const uint8_t WIS6_REPEAT_POSES[] = {14, 15, 16, 17, 18, 19, 18, 17, 16, 15};

static const IdleStep WIFR_STEPS[] = {
  {0, 15, EVENT_NONE},
  {1, 1, EVENT_NONE}, {2, 1, EVENT_NONE}, {3, 1, EVENT_NONE},
  {4, 1, EVENT_NONE}, {5, 1, EVENT_NONE}, {6, 1, EVENT_NONE},
  {7, 1, EVENT_NONE}, {8, 1, EVENT_NONE},
  {9, 9, EVENT_NONE}, {10, 2, EVENT_NONE}, {9, 2, EVENT_NONE},
  {10, 2, EVENT_NONE}, {9, 9, EVENT_NONE},
};

static const uint8_t WIBL_POSES[] = {1, 2, 3, 4, 5, 6, 5, 4, 3, 2, 1, 0};

static uint32_t random_below(CrashIdleController *idle, uint32_t limit) {
  if (limit == 0) return 0;

  /* Crash 1's randa implementation, including its unusual result folding. */
  uint32_t result = 0x41C64E6Du * idle->rng_state + 12345u;
  idle->rng_state = result;
  result /= 15;
  uint32_t result_b = (result + (((uint64_t)result * 33) >> 32)) >> 1;
  uint32_t folded = ((result_b & 0x7C000000u) << 1) - (result_b >> 26);
  uint32_t difference = result - folded;
  uint32_t magnitude = (int32_t)difference < 0 ? 0u - difference : difference;
  return magnitude % limit;
}

static void set_pose(
  CrashIdleController *idle,
  CrashIdleClip clip,
  uint8_t pose,
  uint16_t duration
) {
  idle->clip = clip;
  idle->pose = pose;
  idle->ticks_remaining = duration;
}

static void event_fruit_spawn(CrashIdleController *idle) {
  idle->fruit_active = true;
  debugf("[idle] Wumpa spawn event (visual actor deferred)\n");
}

static void event_palette_activate(CrashIdleController *idle) {
  idle->purple_palette_active = true;
  debugf("[idle] purple palette event (palette data deferred)\n");
}

static void event_cue(void) {
  debugf("[idle] sound cue event (audio deferred)\n");
}

static void cleanup_rare_resources(CrashIdleController *idle) {
  if (idle->fruit_active || idle->purple_palette_active) {
    debugf("[idle] cleanup deferred Wumpa/palette resources\n");
  }
  idle->fruit_active = false;
  idle->purple_palette_active = false;
  idle->spin_yaw = 0.0f;
}

static void begin_ordinary(CrashIdleController *idle, bool initial_entry) {
  cleanup_rare_resources(idle);
  idle->sequence_index = 0;
  idle->repeat_count = 0;
  if (initial_entry) {
    idle->phase = PHASE_ORDINARY_INITIAL_HOLD;
    set_pose(idle, CRASH_CLIP_WIS1, 11, 30);
  } else {
    idle->phase = PHASE_ORDINARY_POSE_12;
    set_pose(idle, CRASH_CLIP_WIS1, 11, 1);
  }
}

static void begin_rare(CrashIdleController *idle) {
  idle->phase = PHASE_RARE_WIS4;
  idle->sequence_index = 0;
  idle->repeat_count = 0;
  idle->spin_direction = idle->mirror_x;
  const IdleStep *step = &WIS4_STEPS[0];
  set_pose(idle, CRASH_CLIP_WIS4, step->pose, step->duration);
}

static void begin_ordinary_motion(CrashIdleController *idle) {
  /* WillC explicitly negates scale.x before poses 1-11 on every pass. */
  idle->mirror_x = -idle->mirror_x;
  set_pose(idle, CRASH_CLIP_WIS1, 0, 1);
}

static void run_step_event(CrashIdleController *idle, uint8_t event) {
  if (event == EVENT_FRUIT_SPAWN) event_fruit_spawn(idle);
  if (event == EVENT_CUE) event_cue();
}

static bool advance_steps(
  CrashIdleController *idle,
  CrashIdleClip clip,
  const IdleStep *steps,
  uint32_t count
) {
  run_step_event(idle, steps[idle->sequence_index].event);
  ++idle->sequence_index;
  if (idle->sequence_index >= count) return true;
  const IdleStep *step = &steps[idle->sequence_index];
  set_pose(idle, clip, step->pose, step->duration);
  return false;
}

static void advance_ordinary(CrashIdleController *idle) {
  switch (idle->phase) {
    case PHASE_ORDINARY_INITIAL_HOLD:
      idle->phase = PHASE_ORDINARY_POSE_12;
      set_pose(idle, CRASH_CLIP_WIS1, 11, 1);
      break;

    case PHASE_ORDINARY_POSE_12:
      if (random_below(idle, 4) == 0) {
        idle->phase = PHASE_ORDINARY_BLINK;
        idle->sequence_index = 0;
        set_pose(idle, CRASH_CLIP_WIS1, BLINK_STEPS[0].pose, BLINK_STEPS[0].duration);
      } else {
        idle->phase = PHASE_ORDINARY_EXTRA_HOLD;
        idle->ticks_remaining = 0;
      }
      break;

    case PHASE_ORDINARY_BLINK:
      if (!advance_steps(
        idle, CRASH_CLIP_WIS1, BLINK_STEPS, ARRAY_COUNT(BLINK_STEPS)
      )) break;
      idle->phase = PHASE_ORDINARY_EXTRA_HOLD;
      idle->ticks_remaining = 0;
      break;

    case PHASE_ORDINARY_EXTRA_HOLD:
      idle->phase = PHASE_ORDINARY_POSES_1_5;
      idle->sequence_index = 0;
      if (random_below(idle, 4) == 0) {
        set_pose(idle, CRASH_CLIP_WIS1, 11, 30);
      } else {
        begin_ordinary_motion(idle);
      }
      break;

    case PHASE_ORDINARY_POSES_1_5:
      if (idle->pose == 11) {
        begin_ordinary_motion(idle);
      } else if (idle->pose < 4) {
        set_pose(idle, CRASH_CLIP_WIS1, idle->pose + 1, 1);
      } else {
        idle->phase = PHASE_ORDINARY_HOLD_6;
        set_pose(idle, CRASH_CLIP_WIS1, 5, 30);
      }
      break;

    case PHASE_ORDINARY_HOLD_6:
      idle->phase = PHASE_ORDINARY_POSES_7_9;
      set_pose(idle, CRASH_CLIP_WIS1, 6, 1);
      break;

    case PHASE_ORDINARY_POSES_7_9:
      if (idle->pose < 8) {
        set_pose(idle, CRASH_CLIP_WIS1, idle->pose + 1, 1);
      } else if (random_below(idle, 8) == 0) {
        idle->phase = PHASE_ORDINARY_REVERSE;
        idle->sequence_index = 0;
        set_pose(idle, CRASH_CLIP_WIS1, 7, 1);
      } else {
        idle->phase = PHASE_ORDINARY_POSES_10_11;
        set_pose(idle, CRASH_CLIP_WIS1, 9, 1);
      }
      break;

    case PHASE_ORDINARY_REVERSE:
      if (idle->sequence_index == 0) {
        idle->sequence_index = 1;
        set_pose(idle, CRASH_CLIP_WIS1, 6, 1);
      } else {
        idle->phase = PHASE_ORDINARY_HOLD_6;
        set_pose(idle, CRASH_CLIP_WIS1, 5, 30);
      }
      break;

    case PHASE_ORDINARY_POSES_10_11:
      if (idle->pose == 9) {
        set_pose(idle, CRASH_CLIP_WIS1, 10, 1);
      } else if (random_below(idle, 50) == 0) {
        begin_rare(idle);
      } else {
        begin_ordinary(idle, false);
      }
      break;

    default:
      break;
  }
}

static void advance_rare(CrashIdleController *idle) {
  switch (idle->phase) {
    case PHASE_RARE_WIS4:
      if (!advance_steps(idle, CRASH_CLIP_WIS4, WIS4_STEPS, ARRAY_COUNT(WIS4_STEPS))) break;
      idle->phase = PHASE_RARE_WIS5;
      idle->sequence_index = 0;
      set_pose(idle, CRASH_CLIP_WIS5, WIS5_STEPS[0].pose, WIS5_STEPS[0].duration);
      break;

    case PHASE_RARE_WIS5:
      if (!advance_steps(idle, CRASH_CLIP_WIS5, WIS5_STEPS, ARRAY_COUNT(WIS5_STEPS))) break;
      idle->phase = PHASE_RARE_WIS6_INTRO;
      idle->sequence_index = 0;
      set_pose(
        idle, CRASH_CLIP_WIS6, WIS6_INTRO_STEPS[0].pose, WIS6_INTRO_STEPS[0].duration
      );
      break;

    case PHASE_RARE_WIS6_INTRO:
      if (!advance_steps(
        idle, CRASH_CLIP_WIS6, WIS6_INTRO_STEPS, ARRAY_COUNT(WIS6_INTRO_STEPS)
      )) break;
      idle->phase = PHASE_RARE_WIS6_REPEAT;
      idle->sequence_index = 0;
      idle->repeat_count = 0;
      set_pose(idle, CRASH_CLIP_WIS6, WIS6_REPEAT_POSES[0], 1);
      break;

    case PHASE_RARE_WIS6_REPEAT:
      ++idle->sequence_index;
      if (idle->sequence_index >= ARRAY_COUNT(WIS6_REPEAT_POSES)) {
        idle->sequence_index = 0;
        ++idle->repeat_count;
      }
      if (idle->repeat_count < 6) {
        set_pose(idle, CRASH_CLIP_WIS6, WIS6_REPEAT_POSES[idle->sequence_index], 1);
      } else {
        event_cue();
        idle->phase = PHASE_RARE_WIS6_TAIL;
        idle->sequence_index = 0;
        set_pose(idle, CRASH_CLIP_WIS6, 14, 1);
      }
      break;

    case PHASE_RARE_WIS6_TAIL:
      if (idle->pose < 18) {
        set_pose(idle, CRASH_CLIP_WIS6, idle->pose + 1, 1);
      } else {
        event_palette_activate(idle);
        idle->phase = PHASE_RARE_WIFR;
        idle->sequence_index = 0;
        set_pose(idle, CRASH_CLIP_WIFR, WIFR_STEPS[0].pose, WIFR_STEPS[0].duration);
      }
      break;

    case PHASE_RARE_WIFR:
      if (!advance_steps(idle, CRASH_CLIP_WIFR, WIFR_STEPS, ARRAY_COUNT(WIFR_STEPS))) break;
      idle->phase = PHASE_RARE_WIBL;
      idle->sequence_index = 0;
      idle->spin_yaw = 0.0f;
      set_pose(idle, CRASH_CLIP_WIBL, WIBL_POSES[0], 1);
      break;

    case PHASE_RARE_WIBL:
      ++idle->sequence_index;
      if (idle->sequence_index < ARRAY_COUNT(WIBL_POSES)) {
        set_pose(idle, CRASH_CLIP_WIBL, WIBL_POSES[idle->sequence_index], 1);
      } else {
        begin_ordinary(idle, false);
      }
      break;

    default:
      break;
  }
}

void crash_idle_init(CrashIdleController *idle, uint32_t random_seed) {
  *idle = (CrashIdleController){
    .rng_state = random_seed ? random_seed : 12345u,
    .mirror_x = 1,
    .spin_direction = 1,
  };
  begin_ordinary(idle, true);
}

void crash_idle_force_rare(CrashIdleController *idle) {
  cleanup_rare_resources(idle);
  idle->suspended = false;
  begin_rare(idle);
}

void crash_idle_update(CrashIdleController *idle, bool player_moving) {
  if (player_moving) {
    if (!idle->suspended) {
      cleanup_rare_resources(idle);
      idle->suspended = true;
      set_pose(idle, CRASH_CLIP_WIS1, 11, 1);
    }
    return;
  }
  if (idle->suspended) {
    idle->suspended = false;
    begin_ordinary(idle, true);
    return;
  }

  if (idle->phase == PHASE_RARE_WIBL) {
    idle->spin_yaw += idle->spin_direction * PS1_ANGLE_TO_RADIANS(0x1C7);
  }
  if (idle->ticks_remaining > 0) --idle->ticks_remaining;
  while (idle->ticks_remaining == 0) {
    if (idle->phase <= PHASE_ORDINARY_POSES_10_11) {
      advance_ordinary(idle);
    } else {
      advance_rare(idle);
    }
  }
}
