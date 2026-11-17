#include "crash_pose.h"

#include <assert.h>
#include <libdragon.h>
#include <stdint.h>
#include <string.h>

typedef struct __attribute__((packed)) {
  char magic[4];
  uint16_t version;
  uint16_t render_vertex_count;
  uint16_t source_vertex_count;
  uint16_t frame_count;
  uint16_t clip_count;
  uint16_t reserved;
} PoseHeader;

typedef struct __attribute__((packed)) {
  uint32_t eid;
  uint16_t first_frame;
  uint16_t frame_count;
} PoseClip;

typedef struct __attribute__((packed)) {
  int16_t position[3];
  uint16_t normal;
} PoseVertex;

static_assert(sizeof(PoseHeader) == 16);
static_assert(sizeof(PoseClip) == 8);
static_assert(sizeof(PoseVertex) == 8);

CrashPoseAsset crash_pose_load(const char *path) {
  int size = 0;
  void *data = asset_load(path, &size);
  assertf(data != NULL, "Could not load pose asset: %s", path);
  assertf(size >= (int)sizeof(PoseHeader), "Truncated pose asset: %s", path);

  const PoseHeader *header = data;
  assertf(memcmp(header->magic, "C64P", 4) == 0, "Invalid pose asset: %s", path);
  assertf(header->version == 1, "Unsupported pose asset version: %d", header->version);
  assertf(header->render_vertex_count > 0, "Pose asset has no render vertices");
  assertf(header->source_vertex_count > 0, "Pose asset has no source vertices");
  assertf(header->frame_count > 0, "Pose asset has no frames");
  assertf(header->clip_count > 0, "Pose asset has no clips");

  size_t clip_bytes = sizeof(PoseClip) * header->clip_count;
  size_t map_bytes = sizeof(uint16_t) * header->render_vertex_count;
  size_t frame_bytes = sizeof(PoseVertex) * header->source_vertex_count * header->frame_count;
  size_t expected_size = sizeof(PoseHeader) + clip_bytes + map_bytes + frame_bytes;
  assertf((size_t)size == expected_size, "Pose asset size mismatch: %s", path);

  const PoseClip *clips = (const PoseClip *)(header + 1);
  const uint16_t *render_map = (const uint16_t *)((const uint8_t *)clips + clip_bytes);
  const PoseVertex *frames = (const PoseVertex *)((const uint8_t *)render_map + map_bytes);
  for (int index = 0; index < header->render_vertex_count; ++index) {
    assertf(render_map[index] < header->source_vertex_count, "Invalid pose render map");
  }
  for (int index = 0; index < header->clip_count; ++index) {
    assertf(
      clips[index].first_frame + clips[index].frame_count <= header->frame_count,
      "Invalid pose clip range"
    );
  }

  return (CrashPoseAsset){
    .data = data,
    .size = size,
    .render_vertex_count = header->render_vertex_count,
    .source_vertex_count = header->source_vertex_count,
    .frame_count = header->frame_count,
    .clip_count = header->clip_count,
    .clips = clips,
    .render_map = render_map,
    .frames = frames,
  };
}

int crash_pose_find_clip(const CrashPoseAsset *asset, uint32_t eid) {
  const PoseClip *clips = asset->clips;
  for (int index = 0; index < asset->clip_count; ++index) {
    if (clips[index].eid == eid) return index;
  }
  return -1;
}

void crash_pose_apply(
  const CrashPoseAsset *asset,
  int clip_index,
  int pose_index,
  T3DVertPacked *vertices
) {
  assertf(clip_index >= 0 && clip_index < asset->clip_count, "Invalid pose clip index");
  const PoseClip *clip = &((const PoseClip *)asset->clips)[clip_index];
  assertf(pose_index >= 0 && pose_index < clip->frame_count, "Invalid pose frame index");
  const PoseVertex *frame = (const PoseVertex *)asset->frames
    + (clip->first_frame + pose_index) * asset->source_vertex_count;

  for (int index = 0; index < asset->render_vertex_count; ++index) {
    const PoseVertex *source = &frame[asset->render_map[index]];
    int16_t *position = t3d_vertbuffer_get_pos(vertices, index);
    position[0] = source->position[0];
    position[1] = source->position[1];
    position[2] = source->position[2];
    *t3d_vertbuffer_get_norm(vertices, index) = source->normal;
  }
}
