// Platform layer: the DS build (platform_nds.c) and a PC test build
// (platform_pc.c) both implement this.
#pragma once
#include "common.h"

enum {
    K_A = 1 << 0, K_B = 1 << 1, K_SELECT = 1 << 2, K_START = 1 << 3,
    K_RIGHT = 1 << 4, K_LEFT = 1 << 5, K_UP = 1 << 6, K_DOWN = 1 << 7,
    K_R = 1 << 8, K_L = 1 << 9, K_X = 1 << 10, K_Y = 1 << 11, K_TOUCH = 1 << 12,
};

extern u16 fb_top[SCREEN_W * SCREEN_H];
extern u16 fb_bot[SCREEN_W * SCREEN_H];

void plat_init(void);
// Show both framebuffers. wait = sync to the 60 Hz refresh.
void plat_present(bool wait);
void plat_poll(void);
u32 plat_keys_down(void);
u32 plat_keys_held(void);
u32 plat_keys_up(void);
// touch position on the bottom screen (valid while K_TOUCH is held)
void plat_touch(int *x, int *y);
u32 plat_ms(void);          // milliseconds since boot
u32 plat_vblanks(void);     // 60 Hz screen refreshes since boot
bool plat_is_dsi(void);
const char *plat_cpu_label(void);
bool plat_running(void);    // false = quit (PC build only)

// Simple file storage on the SD card (fat:/ or sd:/). Paths are relative to
// the game's folder ("dreamlings/").
bool plat_fs_ok(void);
bool plat_file_write(const char *name, const void *data, u32 size);
bool plat_file_append(const char *name, const void *data, u32 size);
s32 plat_file_read(const char *name, void *data, u32 max);   // bytes read or -1
bool plat_file_delete(const char *name);
