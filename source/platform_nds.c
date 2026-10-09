// Nintendo DS platform layer (BlocksDS / libnds)
#ifndef PC_BUILD
#include <nds.h>
#include <fat.h>
#include <stdio.h>
#include <sys/stat.h>
#include "platform.h"

u16 fb_top[SCREEN_W * SCREEN_H] __attribute__((aligned(32)));
u16 fb_bot[SCREEN_W * SCREEN_H] __attribute__((aligned(32)));

static u16 *vram_top, *vram_bot;
static u32 kdown, kheld, kup;
static volatile u32 vblanks;
static bool fs_ok;
static char base_dir[32];
static bool dsi_fast;

static void on_vblank(void) { vblanks++; }
// DSi double speed (declared weak so the build still links on older libnds)
bool setCpuClock(bool speed) __attribute__((weak));

void plat_init(void) {
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
    int bg_main = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    int bg_sub = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    vram_top = bgGetGfxPtr(bg_main);
    vram_bot = bgGetGfxPtr(bg_sub);
    irqSet(IRQ_VBLANK, on_vblank);
    irqEnable(IRQ_VBLANK);
    if (isDSiMode() && setCpuClock) {   // 134 MHz on DSi / 3DS
        setCpuClock(true);
        dsi_fast = true;
    }
    // SD card: DSi SD slot or a flashcart (DLDI)
    if (fatInitDefault()) {
        const char *roots[2] = {"sd:/", "fat:/"};
        for (int i = 0; i < 2 && !fs_ok; i++) {
            char dir[32];
            snprintf(dir, sizeof(dir), "%sdreamlings", roots[i]);
            mkdir(dir, 0777);
            char probe[48];
            snprintf(probe, sizeof(probe), "%s/.probe", dir);
            FILE *f = fopen(probe, "wb");
            if (f) {
                fclose(f);
                remove(probe);
                snprintf(base_dir, sizeof(base_dir), "%s/", dir);
                fs_ok = true;
            }
        }
        if (fs_ok) {
            char d[48];
            snprintf(d, sizeof(d), "%scards", base_dir);
            mkdir(d, 0777);
            snprintf(d, sizeof(d), "%sgifs", base_dir);
            mkdir(d, 0777);
        }
    }
}

void plat_present(bool wait) {
    DC_FlushRange(fb_top, sizeof(fb_top));
    DC_FlushRange(fb_bot, sizeof(fb_bot));
    if (wait) swiWaitForVBlank();
    dmaCopyWords(3, fb_top, vram_top, sizeof(fb_top));
    dmaCopyWords(3, fb_bot, vram_bot, sizeof(fb_bot));
}

void plat_poll(void) {
    scanKeys();
    u32 d = keysDown(), h = keysHeld(), u = keysUp();
    // libnds bit layout matches ours for A..Y; touch is KEY_TOUCH
    kdown = d & 0xFFF;
    kheld = h & 0xFFF;
    kup = u & 0xFFF;
    if (h & KEY_TOUCH) kheld |= K_TOUCH;
    if (d & KEY_TOUCH) kdown |= K_TOUCH;
}
u32 plat_keys_down(void) { return kdown; }
u32 plat_keys_held(void) { return kheld; }
u32 plat_keys_up(void) { return kup; }
void plat_touch(int *x, int *y) {
    touchPosition t;
    touchRead(&t);
    *x = t.px;
    *y = t.py;
}
u32 plat_ms(void) { return (u32)((u64)vblanks * 1000 / 60); }
bool plat_is_dsi(void) { return isDSiMode(); }
const char *plat_cpu_label(void) { return dsi_fast ? "DSi (134 MHz)" : (isDSiMode() ? "DSi" : "DS (67 MHz)"); }
bool plat_running(void) { return true; }

bool plat_fs_ok(void) { return fs_ok; }
static void full_path(char *o, const char *name) { snprintf(o, 96, "%s%s", base_dir, name); }
bool plat_file_write(const char *name, const void *data, u32 size) {
    if (!fs_ok) return false;
    char p[96];
    full_path(p, name);
    FILE *f = fopen(p, "wb");
    if (!f) return false;
    bool ok = size == 0 || fwrite(data, 1, size, f) == size;
    fclose(f);
    return ok;
}
bool plat_file_append(const char *name, const void *data, u32 size) {
    if (!fs_ok) return false;
    char p[96];
    full_path(p, name);
    FILE *f = fopen(p, "ab");
    if (!f) return false;
    bool ok = size == 0 || fwrite(data, 1, size, f) == size;
    fclose(f);
    return ok;
}
s32 plat_file_read(const char *name, void *data, u32 max) {
    if (!fs_ok) return -1;
    char p[96];
    full_path(p, name);
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    s32 n = (s32)fread(data, 1, max, f);
    fclose(f);
    return n;
}
bool plat_file_delete(const char *name) {
    if (!fs_ok) return false;
    char p[96];
    full_path(p, name);
    return remove(p) == 0;
}
#endif
