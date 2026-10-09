// PC test build: runs the game headless from a script and saves screenshots.
//   cc -DPC_BUILD ... -o dreamlings_pc && ./dreamlings_pc script.txt outdir
// Script lines:  <frame> press A|B|X|Y|L|R|START|SELECT|UP|DOWN|LEFT|RIGHT
//                <frame> hold <key> <frames>
//                <frame> tap <x> <y>          (bottom screen)
//                <frame> shot <name>          (writes <name>.ppm with both screens)
//                <frame> quit
#ifdef PC_BUILD
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include "platform.h"

u16 fb_top[SCREEN_W * SCREEN_H];
u16 fb_bot[SCREEN_W * SCREEN_H];

typedef struct { int frame, kind, key, a, b; char name[64]; } Cmd;
static Cmd cmds[4096];
static int ncmds;
static int frame;
static u32 kheld, kdown, kup, prev;
static int tx, ty, touch_left;
static struct { int key, left; } holds[16];
static bool running = true;
static char outdir[256] = ".";
static char sddir[256] = "sdcard/dreamlings";
static u32 compute_ms;

static int key_of(const char *s) {
    static const char *const N[] = {"A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L", "X", "Y"};
    for (int i = 0; i < 12; i++)
        if (!strcmp(s, N[i])) return 1 << i;
    return 0;
}

void pc_load_script(const char *path, const char *out) {
    if (out) { strncpy(outdir, out, sizeof(outdir) - 1); mkdir(outdir, 0777); }
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "no script %s\n", path); return; }
    char line[256];
    while (fgets(line, sizeof(line), f) && ncmds < 4096) {
        Cmd c;
        memset(&c, 0, sizeof(c));
        char what[32] = "", arg[64] = "";
        int n = sscanf(line, "%d %31s %63s %d %d", &c.frame, what, arg, &c.a, &c.b);
        if (n < 2 || line[0] == '#') continue;
        if (!strcmp(what, "press")) { c.kind = 1; c.key = key_of(arg); }
        else if (!strcmp(what, "hold")) { c.kind = 2; c.key = key_of(arg); }
        else if (!strcmp(what, "tap")) { c.kind = 3; c.a = atoi(arg); sscanf(line, "%*d %*s %d %d", &c.a, &c.b); }
        else if (!strcmp(what, "shot")) { c.kind = 4; strcpy(c.name, arg); }
        else if (!strcmp(what, "quit")) c.kind = 5;
        else continue;
        cmds[ncmds++] = c;
    }
    fclose(f);
}

static void shot(const char *name) {
    char p[400];
    snprintf(p, sizeof(p), "%s/%s.ppm", outdir, name);
    FILE *f = fopen(p, "wb");
    if (!f) return;
    fprintf(f, "P6\n256 388\n255\n");
    for (int s = 0; s < 2; s++) {
        const u16 *fb = s ? fb_bot : fb_top;
        for (int i = 0; i < 256 * 192; i++) {
            u16 c = fb[i];
            unsigned char rgb[3] = {(unsigned char)((c & 31) << 3), (unsigned char)(((c >> 5) & 31) << 3), (unsigned char)(((c >> 10) & 31) << 3)};
            fwrite(rgb, 1, 3, f);
        }
        if (!s) {
            unsigned char gap[256 * 4 * 3];
            memset(gap, 20, sizeof(gap));
            fwrite(gap, 1, sizeof(gap), f);
        }
    }
    fclose(f);
}

void plat_init(void) {
    mkdir("sdcard", 0777);
    mkdir(sddir, 0777);
    char d[300];
    snprintf(d, sizeof(d), "%s/cards", sddir);
    mkdir(d, 0777);
    snprintf(d, sizeof(d), "%s/gifs", sddir);
    mkdir(d, 0777);
}

void plat_present(bool wait) {
    (void)wait;
    frame++;
}

void plat_poll(void) {
    u32 now = 0;
    bool tapping = false;
    for (int i = 0; i < 16; i++)
        if (holds[i].left > 0) { now |= holds[i].key; holds[i].left--; }
    for (int i = 0; i < ncmds; i++) {
        Cmd *c = &cmds[i];
        if (c->frame != frame) continue;
        if (c->kind == 1) now |= c->key;
        else if (c->kind == 2) {
            now |= c->key;
            for (int k = 0; k < 16; k++)
                if (holds[k].left <= 0) { holds[k].key = c->key; holds[k].left = c->a - 1; break; }
        } else if (c->kind == 3) { tx = c->a; ty = c->b; touch_left = 2; }
        else if (c->kind == 4) shot(c->name);
        else if (c->kind == 5) running = false;
    }
    if (touch_left > 0) { touch_left--; tapping = true; now |= K_TOUCH; }
    (void)tapping;
    kdown = now & ~prev;
    kup = prev & ~now;
    kheld = now;
    prev = now;
}
u32 plat_keys_down(void) { return kdown; }
u32 plat_keys_held(void) { return kheld; }
u32 plat_keys_up(void) { return kup; }
void plat_touch(int *x, int *y) { *x = tx; *y = ty; }
u32 plat_vblanks(void) { return (u32)frame; }
u32 plat_ms(void) {
    return (u32)(frame * 1000 / 60) + compute_ms;
}
bool plat_is_dsi(void) { return false; }
const char *plat_cpu_label(void) { return "PC test build"; }
bool plat_running(void) { return running && frame < 200000; }

bool plat_fs_ok(void) { return true; }
static void fp(char *o, const char *name) { snprintf(o, 400, "%s/%s", sddir, name); }
bool plat_file_write(const char *name, const void *data, u32 size) {
    char p[400];
    fp(p, name);
    FILE *f = fopen(p, "wb");
    if (!f) return false;
    bool ok = size == 0 || fwrite(data, 1, size, f) == size;
    fclose(f);
    return ok;
}
bool plat_file_append(const char *name, const void *data, u32 size) {
    char p[400];
    fp(p, name);
    FILE *f = fopen(p, "ab");
    if (!f) return false;
    bool ok = size == 0 || fwrite(data, 1, size, f) == size;
    fclose(f);
    return ok;
}
s32 plat_file_read(const char *name, void *data, u32 max) {
    char p[400];
    fp(p, name);
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    s32 n = (s32)fread(data, 1, max, f);
    fclose(f);
    return n;
}
bool plat_file_delete(const char *name) {
    char p[400];
    fp(p, name);
    return remove(p) == 0;
}
#endif
