BUILD_DIR := build
LOCAL_DIR := .local
ASSET_DIR := $(LOCAL_DIR)/assets
TEST_DIR := $(LOCAL_DIR)/tests
T3D_INST := $(realpath $(LOCAL_DIR)/tools/tiny3d)
HOST_CC ?= cc
N64_ROM_EXPANSIONPAK := unused

include $(N64_INST)/include/n64.mk
include $(T3D_INST)/t3d.mk

N64_CFLAGS += -std=gnu2x -O2 -Wall -Wextra

src := src/main.c src/crash_idle.c src/crash_walk.c src/crash_pose.c

assets_png := $(wildcard $(ASSET_DIR)/generated/textures/*.png)
assets_sprites := $(addprefix filesystem/,$(notdir $(assets_png:%.png=%.sprite)))
assets_gltf := $(ASSET_DIR)/generated/models/crash_body.glb \
	$(ASSET_DIR)/generated/models/crash_spin.glb
assets_models := filesystem/crash_body.t3dm filesystem/crash_spin.t3dm
body_pose_objs := $(wildcard $(ASSET_DIR)/private/source/crash/idle/*.obj) \
	$(wildcard $(ASSET_DIR)/private/source/crash/animations/WiWaV/*.obj) \
	$(wildcard $(ASSET_DIR)/private/source/crash/animations/WiS4V/*.obj) \
	$(wildcard $(ASSET_DIR)/private/source/crash/animations/WiS5V/*.obj) \
	$(wildcard $(ASSET_DIR)/private/source/crash/animations/WiS6V/*.obj) \
	$(wildcard $(ASSET_DIR)/private/source/crash/animations/WiFrV/*.obj)
spin_pose_objs := $(wildcard $(ASSET_DIR)/private/source/crash/animations/WiBlV/*.obj)
assets_poses := filesystem/crash_body.poses filesystem/crash_spin.poses
assets_conv := $(assets_sprites) $(assets_models) $(assets_poses)

all: crash64.z64

filesystem/%.sprite: $(ASSET_DIR)/generated/textures/%.png
	@mkdir -p $(dir $@)
	$(N64_MKSPRITE) $(MKSPRITE_FLAGS) -o filesystem "$<"

$(BUILD_DIR)/assets/%.t3dm: $(ASSET_DIR)/generated/models/%.glb $(assets_png)
	@mkdir -p $(dir $@)
	$(T3D_GLTF_TO_3D) "$<" "$@" --base-scale=64 --asset-path=$(ASSET_DIR)/generated/textures

filesystem/%.t3dm: $(BUILD_DIR)/assets/%.t3dm
	@mkdir -p $(dir $@)
	$(N64_BINDIR)/mkasset -c 2 -w 256 -o filesystem $<

$(ASSET_DIR)/generated/poses/crash_body.poses: scripts/pack-crash-poses.py $(BUILD_DIR)/assets/crash_body.t3dm $(body_pose_objs)
	@mkdir -p $(dir $@)
	python3 scripts/pack-crash-poses.py --topology body --model $(BUILD_DIR)/assets/crash_body.t3dm --output $@

$(ASSET_DIR)/generated/poses/crash_spin.poses: scripts/pack-crash-poses.py $(BUILD_DIR)/assets/crash_spin.t3dm $(spin_pose_objs)
	@mkdir -p $(dir $@)
	python3 scripts/pack-crash-poses.py --topology spin --model $(BUILD_DIR)/assets/crash_spin.t3dm --output $@

filesystem/%.poses: $(ASSET_DIR)/generated/poses/%.poses
	@mkdir -p $(dir $@)
	$(N64_BINDIR)/mkasset -c 2 -w 256 -o filesystem $<

$(BUILD_DIR)/crash64.dfs: $(assets_conv)
$(BUILD_DIR)/crash64.elf: $(src:%.c=$(BUILD_DIR)/%.o)

crash64.z64: N64_ROM_TITLE="Crash 64 Tech Demo"
crash64.z64: $(BUILD_DIR)/crash64.dfs

clean:
	rm -rf $(BUILD_DIR) filesystem crash64.z64

test-host:
	$(HOST_CC) -std=gnu2x -Wall -Wextra -Werror -I$(TEST_DIR)/stubs -Isrc \
		src/crash_idle.c $(TEST_DIR)/crash_idle_test.c -lm -o /tmp/crash64-idle-test
	/tmp/crash64-idle-test
	$(HOST_CC) -std=gnu2x -Wall -Wextra -Werror -Isrc \
		src/crash_walk.c $(TEST_DIR)/crash_walk_test.c -o /tmp/crash64-walk-test
	/tmp/crash64-walk-test

-include $(wildcard $(BUILD_DIR)/*.d)

.PHONY: all clean test-host
