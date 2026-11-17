#ifndef CRASH_POSE_H
#define CRASH_POSE_H

#include <stdint.h>

#include <t3d/t3d.h>

typedef struct {
  void *data;
  int size;
  uint16_t render_vertex_count;
  uint16_t source_vertex_count;
  uint16_t frame_count;
  uint16_t clip_count;
  const void *clips;
  const uint16_t *render_map;
  const void *frames;
} CrashPoseAsset;

CrashPoseAsset crash_pose_load(const char *path);
int crash_pose_find_clip(const CrashPoseAsset *asset, uint32_t eid);
void crash_pose_apply(
  const CrashPoseAsset *asset,
  int clip_index,
  int pose_index,
  T3DVertPacked *vertices
);

#endif
