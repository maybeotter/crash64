#include <libdragon.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <t3d/t3d.h>
#include <t3d/t3dmodel.h>

#include "crash_idle.h"
#include "crash_jump.h"
#include "crash_pose.h"
#include "crash_spin.h"
#include "crash_walk.h"

#define ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))
#define FB_COUNT 3
#define PLAYER_BOUND 88.0f
#define STICK_DEAD_ZONE 24
#define PLAYER_MODEL_SCALE 0.096f
#define CAMERA_HEIGHT 13.0f
#define CAMERA_DISTANCE 28.5f
#define CAMERA_LATERAL_OFFSET 1.2f
#define CAMERA_TARGET_HEIGHT 10.5f
#define CAMERA_TARGET_LEAD -2.0f
#define CAMERA_FOV_DEGREES 50.0f
#define GAME_TICK_SECONDS (1.0f / 30.0f)
#define JUMP_INPUT_BUFFER_TICKS 2
#define SPIN_INPUT_BUFFER_TICKS 2
#define SPIN_RECOVERY_INPUT_BUFFER_TICKS 3
#define SPIN_RECOVERY_POSE_KEY_BASE 0x2000u
#define JUMP_POSE_KEY_BASE 0x4000u
#define WALK_POSE_KEY_BASE 0x8000u

#define MAX_SPEED 30.0f
#define ACCEL_TIME_SECONDS (7.23f / 30.0f)
#define DECEL_TIME_SECONDS (4.82f / 30.0f)
#define ACCEL_PER_SECOND (MAX_SPEED / ACCEL_TIME_SECONDS)
#define DECEL_PER_SECOND (MAX_SPEED / DECEL_TIME_SECONDS)
#define DIAGONAL_SPEED_SCALE (327.0f / 256.0f)
#define LOW_SPEED_TURN_THRESHOLD (MAX_SPEED / 1600.0f)
#define TICKS_PER_FRAME 34.0f
#define MAX_SPEED_RAW 0x96000
#define AIR_ACCEL_RAW 0x138800
#define AIR_DECEL_RAW 0x9C400
#define AIR_TURN_RATE_RAW 0xF00
#define AIR_REVERSE_THRESHOLD_RAW 0x7C8
#define RAW_SPEED_TO_WORLD (MAX_SPEED / MAX_SPEED_RAW)
#define AIR_ACCEL_PER_SECOND \
  (AIR_ACCEL_RAW * TICKS_PER_FRAME / 1024.0f \
   * RAW_SPEED_TO_WORLD / GAME_TICK_SECONDS)
#define AIR_DECEL_PER_SECOND \
  (AIR_DECEL_RAW * TICKS_PER_FRAME / 1024.0f \
   * RAW_SPEED_TO_WORLD / GAME_TICK_SECONDS)
#define AIR_TURN_RATE_RADIANS_PER_SECOND \
  (AIR_TURN_RATE_RAW * TICKS_PER_FRAME / 1024.0f \
   * (2.0f * T3D_PI / 4096.0f) / GAME_TICK_SECONDS)
#define AIR_REVERSE_THRESHOLD_RADIANS \
  (AIR_REVERSE_THRESHOLD_RAW * (2.0f * T3D_PI / 4096.0f))
#define SPIN_REJECT_CHANNEL 0
#define SPIN_REJECT_VOLUME (0x666 / (float)0x3FFF)
#define SPIN_REJECT_BASE_RATE 11025.0f
#define SPIN_WHIRL_ALPHA 64

typedef struct {
  float x;
  float z;
  float speed;
  float move_angle;
} PlayerState;

static const uint32_t IDLE_CLIP_EIDS[] = {
  0x7496C0F3, 0x7496C273, 0x7496C2F3, 0x7496C373, 0x74952DF3,
};
static_assert(ARRAY_COUNT(IDLE_CLIP_EIDS) == CRASH_CLIP_WIBL);

static const uint32_t JUMP_CLIP_EIDS[] = {
  CRASH_JUMP_WIF1_EID,
  CRASH_JUMP_WIJU_EID,
  CRASH_JUMP_WIJS_EID,
  CRASH_JUMP_WITS_EID,
};
static_assert(ARRAY_COUNT(JUMP_CLIP_EIDS) == CRASH_JUMP_CLIP_COUNT);

static uint32_t spin_reject_rng = 0x43524153u;

static float get_time_s(void) {
  return (float)((double)get_ticks_us() / 1000000.0);
}

static float clampf(float value, float low, float high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

static void play_spin_reject_sound(wav64_t *sound) {
  spin_reject_rng = spin_reject_rng * 1664525u + 1013904223u;
  uint32_t pitch = 0x399u + (spin_reject_rng >> 24) % 0xCDu;
  wav64_play(sound, SPIN_REJECT_CHANNEL);
  mixer_ch_set_freq(
    SPIN_REJECT_CHANNEL,
    SPIN_REJECT_BASE_RATE * pitch / 1024.0f
  );
  mixer_ch_set_vol(
    SPIN_REJECT_CHANNEL, SPIN_REJECT_VOLUME, SPIN_REJECT_VOLUME
  );
}

static float angle_delta(float from, float to) {
  return atan2f(sinf(to - from), cosf(to - from));
}

static int axis_direction(int value) {
  if (value > STICK_DEAD_ZONE) return 1;
  if (value < -STICK_DEAD_ZONE) return -1;
  return 0;
}

static bool player_update(
  PlayerState *player,
  joypad_inputs_t input,
  float dt,
  bool airborne
) {
  int dir_x = axis_direction(input.stick_x);
  int dir_z = -axis_direction(input.stick_y);
  bool has_input = dir_x != 0 || dir_z != 0;

  if (!has_input) {
    float deceleration = airborne ? AIR_DECEL_PER_SECOND : DECEL_PER_SECOND;
    player->speed -= deceleration * dt;
    if (player->speed < 0.0f) player->speed = 0.0f;
  } else {
    float target_angle = atan2f((float)dir_x, (float)dir_z);
    float delta = fabsf(angle_delta(player->move_angle, target_angle));

    if (airborne) {
      player->speed += cosf(delta) * AIR_ACCEL_PER_SECOND * dt;
      if (player->speed < 0.0f) player->speed = 0.0f;
      if (player->speed <= LOW_SPEED_TURN_THRESHOLD) {
        player->move_angle = target_angle;
      } else if (delta < AIR_REVERSE_THRESHOLD_RADIANS) {
        float turn = clampf(
          angle_delta(player->move_angle, target_angle),
          -AIR_TURN_RATE_RADIANS_PER_SECOND * dt,
          AIR_TURN_RATE_RADIANS_PER_SECOND * dt
        );
        player->move_angle += turn;
      } else {
        player->speed -= AIR_DECEL_PER_SECOND * dt;
        if (player->speed < 0.0f) player->speed = 0.0f;
      }
    } else if (
      player->speed > LOW_SPEED_TURN_THRESHOLD && delta > (T3D_PI * 0.5f)
    ) {
      player->speed = 0.0f;
    } else {
      player->move_angle = target_angle;
      player->speed += ACCEL_PER_SECOND * dt;
    }
    if (player->speed > MAX_SPEED) player->speed = MAX_SPEED;
  }

  bool is_diagonal = has_input && dir_x != 0 && dir_z != 0;
  float speed_scale = is_diagonal ? DIAGONAL_SPEED_SCALE : 1.0f;
  float frame_speed = player->speed * speed_scale * dt;
  player->x += sinf(player->move_angle) * frame_speed;
  player->z += cosf(player->move_angle) * frame_speed;
  player->x = clampf(player->x, -PLAYER_BOUND, PLAYER_BOUND);
  player->z = clampf(player->z, -PLAYER_BOUND, PLAYER_BOUND);
  return has_input;
}

static rspq_block_t *prepare_dynamic_model(
  T3DModel *model,
  T3DVertPacked *vertex_buffers[FB_COUNT]
) {
  size_t vertex_bytes = sizeof(T3DVertPacked) * model->totalVertCount / 2;
  T3DVertPacked *source = t3d_model_get_vertices(model);
  for (int index = 0; index < FB_COUNT; ++index) {
    vertex_buffers[index] = malloc_uncached(vertex_bytes);
    memcpy(vertex_buffers[index], source, vertex_bytes);
  }

  T3DModelIter iterator = t3d_model_iter_create(model, T3D_CHUNK_TYPE_OBJECT);
  while (t3d_model_iter_next(&iterator)) {
    t3d_model_make_object_vert_placeholder(model, iterator.object, T3D_SEGMENT_1);
  }

  rspq_block_begin();
  t3d_model_draw(model);
  t3d_matrix_pop(1);
  return rspq_block_end();
}

static void configure_spin_materials(T3DModel *model) {
  T3DModelIter iterator = t3d_model_iter_create(model, T3D_CHUNK_TYPE_MATERIAL);
  while (t3d_model_iter_next(&iterator)) {
    if (iterator.material->name
        && strstr(iterator.material->name, "Spin_Additive")) {
      /* RDP additive blending wraps overflowing channels instead of
         saturating them, producing green patches over Crash's red fur. */
      iterator.material->blendMode = RDPQ_BLENDER_MULTIPLY;
      iterator.material->primColor.a = SPIN_WHIRL_ALPHA;
    }
  }
}

int main(void) {
  debug_init_isviewer();
  debug_init_usblog();
  asset_init_compression(2);
  dfs_init(DFS_DEFAULT_LOCATION);
  display_init(RESOLUTION_320x240, DEPTH_16_BPP, FB_COUNT, GAMMA_NONE,
               FILTERS_RESAMPLE);
  rdpq_init();
  joypad_init();
  audio_init(44100, 4);
  mixer_init(1);
  mixer_ch_set_limits(SPIN_REJECT_CHANNEL, 0, 48000, 0);
  t3d_init((T3DInitParams){});

  T3DViewport viewport = t3d_viewport_create_buffered(FB_COUNT);
  T3DMat4FP *level_matrix = malloc_uncached(sizeof(T3DMat4FP));
  T3DMat4FP *player_matrices = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT);
  T3DModel *level_model = t3d_model_load("rom:/nsanity_start.t3dm");
  T3DModel *body_model = t3d_model_load("rom:/crash_body.t3dm");
  T3DModel *spin_model = t3d_model_load("rom:/crash_spin.t3dm");
  configure_spin_materials(spin_model);
  CrashPoseAsset body_poses = crash_pose_load("rom:/crash_body.poses");
  CrashPoseAsset spin_poses = crash_pose_load("rom:/crash_spin.poses");
  wav64_t *spin_reject_sound = wav64_load("rom:/spin_reject.wav64", NULL);
  assertf(body_model->totalVertCount == body_poses.render_vertex_count,
          "Body model/pose vertex counts differ");
  assertf(spin_model->totalVertCount == spin_poses.render_vertex_count,
          "Spin model/pose vertex counts differ");

  int idle_clip_indices[ARRAY_COUNT(IDLE_CLIP_EIDS)];
  for (size_t index = 0; index < ARRAY_COUNT(IDLE_CLIP_EIDS); ++index) {
    idle_clip_indices[index] = crash_pose_find_clip(&body_poses, IDLE_CLIP_EIDS[index]);
    assertf(idle_clip_indices[index] >= 0, "Idle pose clip is missing");
  }
  int jump_clip_indices[ARRAY_COUNT(JUMP_CLIP_EIDS)];
  for (size_t index = 0; index < ARRAY_COUNT(JUMP_CLIP_EIDS); ++index) {
    jump_clip_indices[index] = crash_pose_find_clip(&body_poses, JUMP_CLIP_EIDS[index]);
    assertf(jump_clip_indices[index] >= 0, "Jump pose clip is missing");
  }
  int spin_clip_index = crash_pose_find_clip(&spin_poses, 0x7494AAF3);
  assertf(spin_clip_index >= 0, "Spin pose clip is missing");
  int walk_clip_index = crash_pose_find_clip(&body_poses, CRASH_WALK_CLIP_EID);
  assertf(walk_clip_index >= 0, "Walk pose clip is missing");
  int spin_recovery_clip_index = crash_pose_find_clip(
    &body_poses, CRASH_SPIN_WIGS_EID
  );
  assertf(spin_recovery_clip_index >= 0, "Spin recovery pose clip is missing");

  t3d_mat4fp_identity(level_matrix);

  uint8_t ambient[4] = {86, 86, 94, 255};
  uint8_t sun_color[4] = {210, 196, 170, 255};
  fm_vec3_t sun_direction = {{-0.5f, 1.0f, 0.4f}};
  fm_vec3_norm(&sun_direction, &sun_direction);

  PlayerState player = {.move_angle = 0.0f};
  rspq_block_begin();
  t3d_matrix_push(level_matrix);
  t3d_model_draw(level_model);
  t3d_matrix_pop(1);
  rspq_block_t *draw_level = rspq_block_end();
  T3DVertPacked *body_vertex_buffers[FB_COUNT];
  T3DVertPacked *spin_vertex_buffers[FB_COUNT];
  rspq_block_t *draw_body = prepare_dynamic_model(body_model, body_vertex_buffers);
  rspq_block_t *draw_spin = prepare_dynamic_model(spin_model, spin_vertex_buffers);
  uint16_t body_buffer_pose[FB_COUNT] = {UINT16_MAX, UINT16_MAX, UINT16_MAX};
  uint16_t spin_buffer_pose[FB_COUNT] = {UINT16_MAX, UINT16_MAX, UINT16_MAX};
  CrashIdleController idle;
  crash_idle_init(&idle, 12345);
  CrashWalkController walk;
  crash_walk_init(&walk);
  CrashJumpController jump;
  crash_jump_init(&jump);
  CrashSpinController spin;
  crash_spin_init(&spin);
  int frame_index = 0;
  uint8_t jump_input_buffer = 0;
  uint8_t spin_input_buffer = 0;
  bool cull_inverted = false;
  float last_time = get_time_s();
  float game_tick_accumulator = 0.0f;

  for (;;) {
    frame_index = (frame_index + 1) % FB_COUNT;
    joypad_poll();

    float now = get_time_s();
    float dt = clampf(now - last_time, 0.0f, 1.0f / 15.0f);
    last_time = now;
    joypad_inputs_t input = joypad_get_inputs(JOYPAD_PORT_1);
    joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);
    bool has_input = player_update(
      &player, input, dt, crash_jump_is_airborne(&jump)
    );

    bool player_moving = has_input || player.speed > LOW_SPEED_TURN_THRESHOLD;
    if (pressed.a) jump_input_buffer = JUMP_INPUT_BUFFER_TICKS;
    if (pressed.b) {
      spin_input_buffer = spin.phase == CRASH_SPIN_GROUND_RECOVERY
        ? SPIN_RECOVERY_INPUT_BUFFER_TICKS
        : SPIN_INPUT_BUFFER_TICKS;
    }
    if (pressed.c_up && !player_moving && !jump.active
        && spin.phase == CRASH_SPIN_INACTIVE) {
      crash_idle_force_rare(&idle);
    }
    game_tick_accumulator += dt;
    while (game_tick_accumulator >= GAME_TICK_SECONDS) {
      if (jump_input_buffer > 0
          && spin.phase == CRASH_SPIN_GROUND_ACTIVE) {
        crash_jump_start(&jump, false, spin.mirror_x);
        crash_spin_jump_to_air(&spin);
        jump_input_buffer = 0;
      } else if (jump_input_buffer > 0
                 && spin.phase == CRASH_SPIN_INACTIVE
                 && crash_jump_can_start(&jump)) {
        bool from_walk = !jump.active && walk.active;
        int8_t mirror_x = jump.active
          ? jump.mirror_x
          : (walk.active ? walk.mirror_x : idle.mirror_x);
        crash_jump_start(&jump, from_walk, mirror_x);
        jump_input_buffer = 0;
      } else if (jump_input_buffer > 0) {
        --jump_input_buffer;
      }

      if (spin_input_buffer > 0) {
        if (spin.phase == CRASH_SPIN_INACTIVE) {
          if (spin_input_buffer > SPIN_INPUT_BUFFER_TICKS) {
            spin_input_buffer = SPIN_INPUT_BUFFER_TICKS;
          }
          int8_t mirror_x = jump.active
            ? jump.mirror_x
            : (walk.active ? walk.mirror_x : idle.mirror_x);
          if (crash_spin_try_start(
                &spin, crash_jump_is_airborne(&jump), mirror_x
              )) {
            spin_input_buffer = 0;
          } else {
            --spin_input_buffer;
            if (spin_input_buffer == 0 && spin.rejected_input) {
              play_spin_reject_sound(spin_reject_sound);
            }
          }
        } else {
          --spin_input_buffer;
        }
      }

      bool spin_running = spin.phase != CRASH_SPIN_INACTIVE;
      if (spin_running) {
        crash_walk_update(&walk, false);
        crash_idle_update(&idle, true);
        if (jump.active) crash_jump_update(&jump, held.a, has_input);
      } else if (jump.active) {
        crash_walk_update(&walk, false);
        crash_idle_update(&idle, true);
        crash_jump_update(&jump, held.a, has_input);
        if (!jump.active) {
          idle.mirror_x = jump.mirror_x;
          if (player_moving) crash_walk_update(&walk, true);
        }
      } else {
        bool was_walking = walk.active;
        crash_walk_update(&walk, player_moving);
        if (was_walking && !walk.active) idle.mirror_x = walk.mirror_x;
        crash_idle_update(&idle, player_moving);
      }
      crash_spin_update(&spin);
      if (spin_running && spin.phase == CRASH_SPIN_INACTIVE) {
        idle.mirror_x = spin.mirror_x;
        if (!jump.active && player_moving) crash_walk_update(&walk, true);
      }
      game_tick_accumulator -= GAME_TICK_SECONDS;
    }

    int8_t render_mirror_x = spin.phase != CRASH_SPIN_INACTIVE
      ? spin.mirror_x
      : (jump.active
         ? jump.mirror_x
         : (walk.active ? walk.mirror_x : idle.mirror_x));

    fm_vec3_t camera_position = {{
      player.x + CAMERA_LATERAL_OFFSET,
      CAMERA_HEIGHT,
      player.z + CAMERA_DISTANCE,
    }};
    fm_vec3_t camera_target = {{
      player.x + CAMERA_LATERAL_OFFSET,
      CAMERA_TARGET_HEIGHT,
      player.z + CAMERA_TARGET_LEAD,
    }};
    t3d_viewport_set_projection(
      &viewport, T3D_DEG_TO_RAD(CAMERA_FOV_DEGREES), 3.0f, 220.0f
    );
    t3d_viewport_look_at(&viewport, &camera_position, &camera_target,
                         &(fm_vec3_t){{0.0f, 1.0f, 0.0f}});

    float render_yaw = -player.move_angle;
    if (crash_spin_uses_spin_mesh(&spin)) {
      render_yaw += spin.yaw;
    } else if (!jump.active) {
      render_yaw += idle.spin_yaw;
    }
    t3d_mat4fp_from_srt_euler(
      &player_matrices[frame_index],
      (float[3]){
        PLAYER_MODEL_SCALE * render_mirror_x,
        PLAYER_MODEL_SCALE,
        PLAYER_MODEL_SCALE,
      },
      (float[3]){
        0.0f,
        render_yaw,
        0.0f,
      },
      (float[3]){player.x, jump.y, player.z}
    );

    rdpq_attach(display_get(), display_get_zbuf());
    t3d_frame_start();
    bool mirror_x = render_mirror_x < 0;
    if (mirror_x != cull_inverted) {
      t3d_state_set_cull_invert(mirror_x);
      cull_inverted = mirror_x;
    }
    t3d_viewport_attach(&viewport);
    t3d_screen_clear_color(RGBA32(94, 142, 184, 255));
    t3d_screen_clear_depth();
    rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
    t3d_light_set_ambient(ambient);
    t3d_light_set_directional(0, sun_color, &sun_direction);
    t3d_light_set_count(1);
    t3d_state_set_drawflags(T3D_FLAG_SHADED | T3D_FLAG_DEPTH);

    rspq_block_run(draw_level);
    t3d_matrix_push(&player_matrices[frame_index]);
    if (crash_spin_uses_spin_mesh(&spin)) {
      uint16_t pose_key = spin.pose;
      if (spin_buffer_pose[frame_index] != pose_key) {
        crash_pose_apply(
          &spin_poses, spin_clip_index, spin.pose, spin_vertex_buffers[frame_index]
        );
        spin_buffer_pose[frame_index] = pose_key;
      }
      t3d_segment_set(T3D_SEGMENT_1, spin_vertex_buffers[frame_index]);
      rspq_block_run(draw_spin);
    } else if (crash_spin_uses_recovery_mesh(&spin)) {
      uint16_t pose_key = SPIN_RECOVERY_POSE_KEY_BASE | spin.pose;
      if (body_buffer_pose[frame_index] != pose_key) {
        crash_pose_apply(
          &body_poses,
          spin_recovery_clip_index,
          spin.pose,
          body_vertex_buffers[frame_index]
        );
        body_buffer_pose[frame_index] = pose_key;
      }
      t3d_segment_set(T3D_SEGMENT_1, body_vertex_buffers[frame_index]);
      rspq_block_run(draw_body);
    } else if (jump.active) {
      uint16_t pose_key = JUMP_POSE_KEY_BASE
        | ((uint16_t)jump.clip << 8)
        | jump.pose;
      if (body_buffer_pose[frame_index] != pose_key) {
        crash_pose_apply(
          &body_poses,
          jump_clip_indices[jump.clip],
          jump.pose,
          body_vertex_buffers[frame_index]
        );
        body_buffer_pose[frame_index] = pose_key;
      }
      t3d_segment_set(T3D_SEGMENT_1, body_vertex_buffers[frame_index]);
      rspq_block_run(draw_body);
    } else if (walk.active) {
      uint16_t pose_key = WALK_POSE_KEY_BASE | walk.pose;
      if (body_buffer_pose[frame_index] != pose_key) {
        crash_pose_apply(
          &body_poses, walk_clip_index, walk.pose, body_vertex_buffers[frame_index]
        );
        body_buffer_pose[frame_index] = pose_key;
      }
      t3d_segment_set(T3D_SEGMENT_1, body_vertex_buffers[frame_index]);
      rspq_block_run(draw_body);
    } else if (idle.clip == CRASH_CLIP_WIBL) {
      uint16_t pose_key = idle.pose;
      if (spin_buffer_pose[frame_index] != pose_key) {
        crash_pose_apply(
          &spin_poses, spin_clip_index, idle.pose, spin_vertex_buffers[frame_index]
        );
        spin_buffer_pose[frame_index] = pose_key;
      }
      t3d_segment_set(T3D_SEGMENT_1, spin_vertex_buffers[frame_index]);
      rspq_block_run(draw_spin);
    } else {
      int clip_index = idle_clip_indices[idle.clip];
      uint16_t pose_key = ((uint16_t)idle.clip << 8) | idle.pose;
      if (body_buffer_pose[frame_index] != pose_key) {
        crash_pose_apply(
          &body_poses, clip_index, idle.pose, body_vertex_buffers[frame_index]
        );
        body_buffer_pose[frame_index] = pose_key;
      }
      t3d_segment_set(T3D_SEGMENT_1, body_vertex_buffers[frame_index]);
      rspq_block_run(draw_body);
    }
    rdpq_detach_show();
    mixer_try_play();
  }
}
