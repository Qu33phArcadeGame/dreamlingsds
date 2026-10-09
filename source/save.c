// Save game on the SD card (dreamlings/save.dat)
#include "game.h"
#include "dreamnet.h"

SaveData G;
bool save_dirty;

void save_defaults(void) {
    memset(&G, 0, sizeof(G));
    G.magic = SAVE_MAGIC;
    G.version = SAVE_VERSION;
    G.map = MAP_MAP;
    G.px = 255; // use the map's start spawn
    G.face = 0;
    GenSettings *s = &G.gen;
    s->path = PATH_STRAIGHT;
    s->frames = 24;
    s->zoom = 9700;
    s->steps = 3;
    s->lr = 45;
    s->range = 80;
    s->seg = 8;
    s->start_steps = 8;
    s->sharpen = 25;
    s->layers = DN_MID;
    s->groups = 15;
    s->detail = 1;
    s->play_fps = 12;
    s->play_fps_out = 12;
    s->boomerang = 1;
    s->book = 0;   // upright; Rotate in the generator switches to book mode
    s->left_handed = 0;
}

bool save_load(void) {
    SaveData tmp;
    s32 n = plat_file_read("save.dat", &tmp, sizeof(tmp));
    if (n != (s32)sizeof(tmp) || tmp.magic != SAVE_MAGIC || tmp.version != SAVE_VERSION) return false;
    G = tmp;
    if (G.nfus > MAX_FUSIONS) G.nfus = 0;
    if (G.map >= NUM_MAPS) G.map = MAP_MAP;
    return true;
}

bool save_write(void) {
    save_dirty = false;
    return plat_file_write("save.dat", &G, sizeof(G));
}

void add_fusion(const Fusion *f) {
    if (G.nfus >= MAX_FUSIONS) {
        // binder is full: drop the weakest common fusion to make room
        int worst = 0;
        for (int i = 1; i < G.nfus; i++)
            if (G.fus[i].rare < G.fus[worst].rare || (G.fus[i].rare == G.fus[worst].rare && G.fus[i].power < G.fus[worst].power)) worst = i;
        memmove(&G.fus[worst], &G.fus[worst + 1], (G.nfus - worst - 1) * sizeof(Fusion));
        G.nfus--;
        if (G.companion.kind == 2) {
            if (G.companion.fus == worst) G.companion.kind = 0;
            else if (G.companion.fus > worst) G.companion.fus--;
        }
    }
    G.fus[G.nfus++] = *f;
    for (int i = 0; i < f->n; i++) {
        int n = f->comp[i];
        if (n >= 1 && n <= NUM_DREAMLINGS) {
            G.caught[n] = 1;
            G.tiers[n] |= 1 << f->tier[i];
        }
    }
    save_dirty = true;
}
