// The Dream Generator building: dream a trading card, then zoom into it
// forever (straight / square / triangle), play it back as a zoomerang,
// keep it in the gallery or save it to the SD card as a GIF.
#include "game.h"
#include "card.h"
#include "dreamnet.h"
#include "gif.h"
#include <stdlib.h>

// ---------------------------------------------------------------- state
static const DNModel *model;
static int model_idx;
static Pick seed;
static int W = 40, H = 50;                 // dream size
static s16 cur[DN_MAX_W * DN_MAX_H * 3];   // the dream being worked on (Q12)
static s16 tmpimg[DN_MAX_W * DN_MAX_H * 3];
static bool have_cur;
static u16 seed_art[DN_MAX_W * DN_MAX_H];
static u16 live_art[DN_MAX_W * DN_MAX_H];
// Frames live in one big block from the heap: as much as the console can spare
// (a DSi has 16 MB, an original DS 4 MB).
static u16 *pool;
static u32 pool_pixels;
static int nframes;
static int max_frames(void) { return (int)(pool_pixels / (u32)(W * H)); }
static void pool_alloc(void) {
    if (pool) return;
    static const u32 TRY_KB[] = {8192, 6144, 4096, 2048, 1536, 1024, 768, 512, 384, 256, 128};
    for (unsigned i = 0; i < sizeof(TRY_KB) / sizeof(TRY_KB[0]); i++) {
        u32 bytes = TRY_KB[i] * 1024;
        void *p = malloc(bytes);
        if (!p) continue;
        void *spare = malloc(192 * 1024);   // leave room for the SD card code
        if (!spare) { free(p); continue; }
        free(spare);
        pool = (u16 *)p;
        pool_pixels = bytes / 2;
        return;
    }
}
static u16 *frame_ptr(int i) { return pool + i * W * H; }

typedef struct {
    u16 frames;
    u8 path, steps, seg, sharpen, layers, groups;
    u16 zoom, lr;
    u8 range, pad;
} Run;
#define MAX_RUNS 12
static struct {
    u8 start_steps, start_layers, start_groups, pad;
    u16 start_lr;
    u8 nruns, pad2;
    Run runs[MAX_RUNS];
} recipe;

static u16 card_no;
static bool dirty, kept;
static char status[96];
static int progress = -1;    // 0..256, -1 hidden

// work queue
enum { W_IDLE, W_START, W_ZOOM };
static int work;
static int start_oct, start_i, oct_h, oct_w;
static int zoom_left, zoom_total, zoom_step_i, path_frame;
static bool stop_req;
static u32 ms_per_mac_q16;   // measured speed
static u32 play_t0;

// pages
enum { P_MAIN, P_SETTINGS, P_PICK, P_GALLERY };
static int page;
static int settings_scroll, pick_scroll, gallery_scroll;
#define card_buf card_shared
#define portrait portrait_shared

// gallery index on the SD card
typedef struct { u16 no; char name[24]; u8 rarity, w, h, model; u16 nframes; } GalleryEntry;
#define MAX_GALLERY 48
static GalleryEntry gallery[MAX_GALLERY];
static int ngallery;

static const GenSettings *S(void) { return &G.gen; }
static const char *const PATH_NAME[3] = {"Straight", "Square", "Triangle"};

// ---------------------------------------------------------------- presets (tuned for the DS)
typedef struct { const char *name; u8 path, frames; u16 zoom; u8 steps; u16 lr; u8 range, seg, start, sharpen, layers; } Preset;
static const Preset PRESETS[] = {
    {"Quick dive", PATH_STRAIGHT, 24, 9600, 3, 45, 0, 8, 8, 35, DN_MID},
    {"Eye storm", PATH_SQUARE, 32, 9700, 4, 50, 100, 8, 10, 35, DN_MID | DN_HI},
    {"Triangle trip", PATH_TRIANGLE, 36, 9700, 3, 45, 120, 10, 8, 35, DN_LO | DN_MID},
    {"Swirl melt", PATH_STRAIGHT, 40, 9500, 2, 60, 0, 8, 6, 40, DN_LO},
    {"Deep beasts", PATH_SQUARE, 24, 9750, 6, 60, 80, 10, 14, 30, DN_HI},
    {"Slow drift", PATH_SQUARE, 48, 9900, 3, 30, 40, 15, 10, 30, DN_MID | DN_HI},
};
#define NPRESETS (int)(sizeof(PRESETS) / sizeof(PRESETS[0]))
static int preset_idx = -1;

// ---------------------------------------------------------------- small text helpers
static void num_str(char *o, int v) {
    char d[12];
    int n = 0;
    bool neg = v < 0;
    if (neg) v = -v;
    do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    if (neg) *o++ = '-';
    while (n) *o++ = d[--n];
    *o = 0;
}
// fixed-point x/scale as text, e.g. fx_str(o, 9700, 10000, 4) -> "0.97"
static void fx_str(char *o, int v, int scale, int decimals) {
    num_str(o, v / scale);
    char *e = o + strlen(o);
    int frac = v % scale;
    if (decimals > 0) {
        *e++ = '.';
        int s = scale;
        for (int i = 0; i < decimals; i++) {
            s /= 10;
            *e++ = (char)('0' + (s ? (frac / s) % 10 : 0));
        }
        while (e[-1] == '0' && e[-2] != '.') e--;
    }
    *e = 0;
}
static void layers_str(char *o, u8 layers, u8 groups) {
    o[0] = 0;
    for (int h = 0; h < 3; h++) {
        if (!(layers & (1 << h))) continue;
        if (o[0]) strcat(o, "+");
        strcat(o, model->head_label[h]);
    }
    if ((groups & 15) && (groups & 15) != 15) {
        strcat(o, " (");
        bool first = true;
        int h = (layers & DN_HI) ? 2 : (layers & DN_MID) ? 1 : 0;
        for (int g = 0; g < 4; g++)
            if (groups & (1 << g)) {
                if (!first) strcat(o, ",");
                strcat(o, model->group_label[h][g]);
                first = false;
            }
        strcat(o, ")");
    }
}

// ---------------------------------------------------------------- recipe (synced to playback)
static void card_info(CardInfo *ci, int frame_idx) {
    memset(ci, 0, sizeof(*ci));
    pick_name(&seed, ci->name);
    pick_detail(&seed, ci->detail);
    ci->rarity = (u8)pick_rarity(&seed);
    ci->number = card_no;
    ci->frames = (u16)nframes;
    char ls[48], a[16], b[16], c[16];
    char foot[64];
    strcpy(foot, W == 40 ? "40X50 ON " : "80X100 ON ");
    strcat(foot, model->name);
    strcat(foot, " DS, ");
    num_str(a, S()->play_fps);
    strcat(foot, a);
    strcat(foot, " FPS");
    if (S()->boomerang) strcat(foot, " \x07");
    if (!nframes) {
        layers_str(ls, S()->layers, S()->groups);
        strcpy(ci->style, model->name);
        strcat(ci->style, " DS");
        return;
    }
    int total = 1;
    for (int i = 0; i < recipe.nruns; i++) total += recipe.runs[i].frames;
    if (frame_idx >= total) frame_idx = total - 1;
    int start = 1;
    for (int i = 0; i < recipe.nruns; i++) {
        const Run *z = &recipe.runs[i];
        if (frame_idx >= start && frame_idx < start + z->frames) {
            strcpy(ci->bullets[0], "ZOOM ");
            num_str(a, i + 1); strcat(ci->bullets[0], a);
            strcat(ci->bullets[0], " OF ");
            num_str(a, recipe.nruns); strcat(ci->bullets[0], a);
            strcat(ci->bullets[0], ": FRAME ");
            num_str(a, frame_idx - start + 1); strcat(ci->bullets[0], a);
            strcat(ci->bullets[0], " OF ");
            num_str(a, z->frames); strcat(ci->bullets[0], a);
            char *l = ci->bullets[1];
            strcpy(l, PATH_NAME[z->path]);
            strcat(l, " ZOOM X");
            fx_str(a, z->zoom, 10000, 4); strcat(l, a);
            strcat(l, ", ");
            num_str(b, z->steps); strcat(l, b);
            strcat(l, " STEPS, LR ");
            fx_str(c, z->lr, 1000, 3); strcat(l, c);
            if (z->path != PATH_STRAIGHT) {
                strcat(l, ", RANGE ");
                fx_str(a, z->range, 1000, 3); strcat(l, a);
                strcat(l, " ");
                num_str(a, z->seg); strcat(l, a);
                strcat(l, "/SIDE");
            }
            if (z->sharpen) { strcat(l, ", SHARPEN "); fx_str(a, z->sharpen, 100, 2); strcat(l, a); }
            layers_str(ls, z->layers, z->groups);
            strcpy(ci->bullets[2], "LAYERS ");
            strcat(ci->bullets[2], ls);
            strcpy(ci->bullets[3], foot);
            ci->nbullets = 4;
            strcpy(ci->style, PATH_NAME[z->path]);
            strcat(ci->style, " zoom, ");
            strcat(ci->style, ls);
            return;
        }
        start += z->frames;
    }
    layers_str(ls, recipe.start_layers, recipe.start_groups);
    if (recipe.start_steps) {
        strcpy(ci->bullets[0], "STARTING DREAM: ");
        num_str(a, recipe.start_steps); strcat(ci->bullets[0], a);
        strcat(ci->bullets[0], " STEPS X 3 SIZES");
        strcpy(ci->bullets[1], "LR ");
        fx_str(a, recipe.start_lr, 1000, 3); strcat(ci->bullets[1], a);
        strcat(ci->bullets[1], ", LAYERS ");
        strcat(ci->bullets[1], ls);
    } else {
        strcpy(ci->bullets[0], "STARTING CARD");
        ci->bullets[1][0] = 0;
    }
    if (recipe.nruns) {
        num_str(a, recipe.nruns);
        strcpy(ci->bullets[2], a);
        strcat(ci->bullets[2], recipe.nruns > 1 ? " ZOOM RUNS FOLLOW" : " ZOOM RUN FOLLOWS");
    } else strcpy(ci->bullets[2], "NO ZOOM YET");
    strcpy(ci->bullets[3], foot);
    ci->nbullets = 4;
    strcpy(ci->style, recipe.start_steps ? "Starting dream, " : "Starting card, ");
    strcat(ci->style, ls);
}

// ---------------------------------------------------------------- dreaming
static DNObjective objective(u8 layers, u8 groups) {
    DNObjective o;
    o.layers = layers ? layers : DN_MID;
    o.groups = groups;
    return o;
}

// ---------------------------------------------------------------- floating dreams
// When you're done with a dream (leave the building, switch cards, load another),
// it floats out of the door into town as the creature's outline with the dream inside.
static bool floated;   // this dream is already out in town
static bool release_dream(void) {
    if (floated || nframes < 2 || !seed.kind) return false;
    const Area *A = area_for_map(G.map);
    int x = A->gen_x >= 0 ? A->gen_x * TILE_SIZE + TILE_SIZE * 3 / 2 : 64;
    int y = A->gen_y >= 0 ? A->gen_y * TILE_SIZE + TILE_SIZE * 2 + 6 : 64;
    int s = floater_begin(G.map, &seed, x, y);
    // 8 frames spread over the dream (frame 0 is the plain card, so skip it)
    for (int k = 0; k < 8; k++) {
        int fi = 1 + (nframes - 2) * k / 7;
        if (k && fi == 1 + (nframes - 2) * (k - 1) / 7) continue;   // short dream: no repeats
        floater_frame(s, frame_ptr(fi), W, H);
    }
    floater_end(s);
    floated = true;
    return true;
}
static void leave_generator(void) {
    char nm[32];
    pick_name(&seed, nm);
    bool out = release_dream();
    save_write();
    ui_set_touch_xform(0);
    set_mode(MODE_WORLD);
    world_resume();
    if (out) {
        char m[96];
        strcpy(m, "Your dream of ");
        strcat(m, nm);
        strcat(m, " floated out into town!");
        world_say(m);
    }
}

static void set_seed(const Pick *p) {
    release_dream();
    floated = false;
    seed = *p;
    W = S()->detail ? 80 : 40;
    H = S()->detail ? 100 : 50;
    make_card_art(&seed, seed_art, W, H);
    memcpy(live_art, seed_art, W * H * 2);
    nframes = 0;
    have_cur = false;
    recipe.nruns = 0;
    recipe.start_steps = 0;
    card_no = 0;
    dirty = false;
    kept = false;
    work = W_IDLE;
    path_frame = 0;
}

static void timed_step(s16 *img, int h, int w, int lr, DNObjective o) {
    u32 t0 = plat_ms();
    // deeper layers leave a fine checkerboard on tiny pictures: smooth them a bit more
    dn_step(model, img, h, w, lr, o, w >= 60 ? 6 : 3, (o.layers & (DN_MID | DN_HI)) ? 2 : 1);
    u32 dt = plat_ms() - t0;
    u32 cost = dn_step_cost(model, h, w);
    if (cost && dt) {
        u32 v = (u32)(((u64)dt << 16) * 1000 / cost);  // ms per 1000 MACs, Q16
        ms_per_mac_q16 = ms_per_mac_q16 ? (ms_per_mac_q16 * 3 + v) / 4 : v;
    }
}

static void push_frame(void) {
    if (nframes >= max_frames()) return;
    dn_to_rgb15(cur, frame_ptr(nframes), W * H);
    nframes++;
    dirty = true;
}

// begin a new card: the undreamed card art is frame 1 (the dreaming happens while zooming)
static void start_dream(void) {
    if (!seed.kind) return;
    G.card_no++;
    card_no = G.card_no;
    save_dirty = true;
    dn_from_rgb15(seed_art, cur, W * H);
    recipe.start_steps = 0;   // no starting dream: the plain card is frame 1
    recipe.start_layers = S()->layers;
    recipe.start_groups = S()->groups;
    recipe.start_lr = S()->lr;
    recipe.nruns = 0;
    nframes = 0;
    have_cur = true;
    start_oct = 2;
    start_i = 0;
    push_frame();
    work = W_IDLE;
}

static void start_work_step(void) {
    DNObjective o = objective(S()->layers, S()->groups);
    timed_step(cur, oct_h, oct_w, S()->lr * 4096 / 1000, o);
    start_i++;
    int total = S()->start_steps * 3, done = (2 - start_oct) * S()->start_steps + start_i;
    progress = done * 256 / MAX(1, total);
    strcpy(status, "Dreaming... detail layer ");
    char a[4];
    num_str(a, 3 - start_oct);
    strcat(status, a);
    strcat(status, " of 3");
    // show the live dream (upscaled to the card size)
    dn_resize(cur, oct_h, oct_w, tmpimg, H, W);
    dn_to_rgb15(tmpimg, live_art, W * H);
    if (start_i >= S()->start_steps) {
        start_i = 0;
        start_oct--;
        int nh, nw;
        if (start_oct < 0) { nh = H; nw = W; }
        else if (start_oct == 1) { nh = MAX(16, H * 100 / 135); nw = MAX(16, W * 100 / 135); }
        else { nh = H; nw = W; }
        dn_resize(cur, oct_h, oct_w, tmpimg, nh, nw);
        memcpy(cur, tmpimg, nh * nw * 3 * sizeof(s16));
        oct_h = nh;
        oct_w = nw;
        if (start_oct < 0) {
            push_frame();
            work = W_IDLE;
            progress = -1;
            strcpy(status, "Dreamed! Press Zoom to fall into the card.");
        }
    }
}

static void path_offset(int n, int *ox_q8, int *oy_q8) {
    const GenSettings *s = S();
    int seg = MAX(1, s->seg), sub = (n % seg) * 256 / seg;  // 0..255
    int rx = W * s->range * 128 / 1000, ry = H * s->range * 128 / 1000;  // half range, Q8 pixels
    *ox_q8 = *oy_q8 = 0;
    if (s->path == PATH_SQUARE) {
        int ph = (n % (4 * seg)) / seg;
        if (ph == 0) { *ox_q8 = -rx + 2 * rx * sub / 256; *oy_q8 = -ry; }
        else if (ph == 1) { *ox_q8 = rx; *oy_q8 = -ry + 2 * ry * sub / 256; }
        else if (ph == 2) { *ox_q8 = rx - 2 * rx * sub / 256; *oy_q8 = ry; }
        else { *ox_q8 = -rx; *oy_q8 = ry - 2 * ry * sub / 256; }
    } else if (s->path == PATH_TRIANGLE) {
        int ph = (n % (3 * seg)) / seg;
        static const int VX[3] = {0, 222, -222}, VY[3] = {-256, 128, 128}; // unit triangle (sqrt3/2 ~ 222/256)
        int ax = VX[ph] * rx / 256, ay = VY[ph] * ry / 256, bx = VX[(ph + 1) % 3] * rx / 256, by = VY[(ph + 1) % 3] * ry / 256;
        *ox_q8 = ax + (bx - ax) * sub / 256;
        *oy_q8 = ay + (by - ay) * sub / 256;
    }
}

static void start_zoom(void) {
    if (!nframes || nframes >= max_frames()) return;
    if (!have_cur) { dn_from_rgb15(frame_ptr(nframes - 1), cur, W * H); have_cur = true; }
    const GenSettings *s = S();
    Run r = {0, s->path, s->steps, s->seg, s->sharpen, s->layers, s->groups, s->zoom, s->lr, s->range, 0};
    Run *last = recipe.nruns ? &recipe.runs[recipe.nruns - 1] : 0;
    bool same = last && last->path == r.path && last->steps == r.steps && last->seg == r.seg && last->sharpen == r.sharpen &&
                last->layers == r.layers && last->groups == r.groups && last->zoom == r.zoom && last->lr == r.lr && last->range == r.range;
    if (!same && recipe.nruns < MAX_RUNS) recipe.runs[recipe.nruns++] = r;  // (when full, the last run keeps counting)
    zoom_total = MIN((int)s->frames, max_frames() - nframes);
    zoom_left = zoom_total;
    zoom_step_i = 0;
    work = W_ZOOM;
    stop_req = false;
}

static void zoom_work_step(void) {
    const GenSettings *s = S();
    DNObjective o = objective(s->layers, s->groups);
    if (zoom_step_i == 0) {
        path_frame++;
        int ox, oy;
        path_offset(path_frame, &ox, &oy);
        // a random sub-pixel nudge each frame: spreads the resampling blur evenly,
        // so "keep sharp" can't carve a cross into the middle of the zoom
        ox += (int)(rnd() % 257) - 128;
        oy += (int)(rnd() % 257) - 128;
        dn_zoom(cur, tmpimg, H, W, (int)((u32)s->zoom * 65536 / 10000), ox, oy);
        memcpy(cur, tmpimg, W * H * 3 * sizeof(s16));
        if (s->sharpen) dn_sharpen(cur, H, W, s->sharpen * 256 / 100);
    }
    if (s->steps) timed_step(cur, H, W, s->lr * 4096 / 1000, o);
    zoom_step_i++;
    dn_to_rgb15(cur, live_art, W * H);
    if (zoom_step_i >= MAX(1, s->steps)) {
        zoom_step_i = 0;
        push_frame();
        recipe.runs[recipe.nruns - 1].frames++;
        zoom_left--;
        int done = zoom_total - zoom_left;
        progress = done * 256 / MAX(1, zoom_total);
        strcpy(status, PATH_NAME[s->path]);
        strcat(status, " zoom... frame ");
        char a[8];
        num_str(a, done); strcat(status, a);
        strcat(status, " of ");
        num_str(a, zoom_total); strcat(status, a);
        if (zoom_left <= 0 || stop_req || nframes >= max_frames()) {
            work = W_IDLE;
            progress = -1;
            play_t0 = plat_ms();
            if (nframes >= max_frames()) strcpy(status, "The card is full. Keep it or save a GIF!");
            else { num_str(a, nframes); strcpy(status, a); strcat(status, " frames on the card. Zoom again to keep falling."); }
        }
    }
}

// ---------------------------------------------------------------- playback
static int play_frame(void) {
    if (nframes <= 1) return nframes ? 0 : -1;
    u32 t = plat_ms() - play_t0;
    int fi = MAX(2, S()->play_fps), fo = MAX(2, S()->play_fps_out);
    if (!S()->boomerang || nframes <= 2) return (int)((t * fi / 1000) % nframes);
    u32 tin = (u32)nframes * 1000 / fi, tout = (u32)(nframes - 2) * 1000 / fo;
    u32 tc = t % (tin + tout);
    if (tc < tin) return MIN(nframes - 1, (int)(tc * fi / 1000));
    int k = (int)((tc - tin) * fo / 1000);
    return MAX(1, nframes - 2 - k);
}

// ---------------------------------------------------------------- SD card: gallery + GIF
static void gallery_load(void) {
    ngallery = 0;
    s32 n = plat_file_read("cards/index.dat", gallery, sizeof(gallery));
    if (n > 0) ngallery = n / (s32)sizeof(GalleryEntry);
}
static void card_file(char *o, int no) {
    strcpy(o, "cards/c");
    char a[8];
    num_str(a, 10000 + no);
    strcat(o, a + 1);
    strcat(o, ".ddc");
}
typedef struct {
    u32 magic;
    u16 no, nframes;
    u8 w, h, model, rarity;
    Pick seed;
    u8 play_fps, play_fps_out, boomerang, pad;
    u8 recipe[sizeof(recipe)];
    Fusion fus; // copy of the fusion (the binder may change later)
} CardHeader;
#define CARD_MAGIC 0x44524344u

static bool keep_card(void) {
    if (!nframes || !plat_fs_ok()) return false;
    char path[40];
    card_file(path, card_no);
    CardHeader hd;
    memset(&hd, 0, sizeof(hd));
    hd.magic = CARD_MAGIC;
    hd.no = card_no;
    hd.nframes = (u16)nframes;
    hd.w = (u8)W;
    hd.h = (u8)H;
    hd.model = (u8)model_idx;
    hd.rarity = (u8)pick_rarity(&seed);
    hd.seed = seed;
    hd.play_fps = S()->play_fps;
    hd.play_fps_out = S()->play_fps_out;
    hd.boomerang = S()->boomerang;
    memcpy(hd.recipe, &recipe, sizeof(recipe));
    if (seed.kind == 2 && seed.fus < G.nfus) hd.fus = G.fus[seed.fus];
    if (!plat_file_write(path, &hd, sizeof(hd))) return false;
    for (int i = 0; i < nframes; i += 16) {
        int n = MIN(16, nframes - i);
        if (!plat_file_append(path, frame_ptr(i), (u32)n * W * H * 2)) return false;
    }
    gallery_load();
    int slot = -1;
    for (int i = 0; i < ngallery; i++)
        if (gallery[i].no == card_no) slot = i;
    if (slot < 0) {
        if (ngallery >= MAX_GALLERY) { memmove(&gallery[0], &gallery[1], (MAX_GALLERY - 1) * sizeof(GalleryEntry)); ngallery--; }
        slot = ngallery++;
    }
    GalleryEntry *e = &gallery[slot];
    memset(e, 0, sizeof(*e));
    e->no = card_no;
    char nm[32];
    pick_name(&seed, nm);
    memcpy(e->name, nm, 23);
    e->name[23] = 0;
    e->rarity = hd.rarity;
    e->w = (u8)W;
    e->h = (u8)H;
    e->model = (u8)model_idx;
    e->nframes = (u16)nframes;
    plat_file_write("cards/index.dat", gallery, (u32)ngallery * sizeof(GalleryEntry));
    kept = true;
    dirty = false;
    return true;
}

static bool load_card(int gi) {
    release_dream();
    floated = false;
    char path[40];
    card_file(path, gallery[gi].no);
    static CardHeader hd;
    s32 n = plat_file_read(path, &hd, sizeof(hd));
    if (n != (s32)sizeof(hd) || hd.magic != CARD_MAGIC) return false;
    // read header + frames in one go into the pool
    static u8 hdrbuf[sizeof(CardHeader)];
    (void)hdrbuf;
    W = hd.w;
    H = hd.h;
    int want = MIN((int)hd.nframes, max_frames());
    // the platform reads whole files; read into the pool after the header
    u32 total = sizeof(hd) + (u32)want * W * H * 2;
    static u8 *buf;
    buf = (u8 *)pool;   // reuse the frame pool as the file buffer, then shift
    if (total > pool_pixels * 2) total = pool_pixels * 2;
    s32 got = plat_file_read(path, buf, total);
    if (got < (s32)sizeof(hd)) return false;
    memmove(pool, buf + sizeof(hd), (u32)got - sizeof(hd));
    nframes = (int)(((u32)got - sizeof(hd)) / (W * H * 2));
    seed = hd.seed;
    if (seed.kind == 2) {
        // the fusion may have moved in the binder: find it again
        int found = -1;
        for (int i = 0; i < G.nfus; i++)
            if (!memcmp(&G.fus[i], &hd.fus, sizeof(Fusion))) found = i;
        if (found >= 0) seed.fus = (u16)found;
        else if (G.nfus < MAX_FUSIONS) { add_fusion(&hd.fus); seed.fus = (u16)(G.nfus - 1); }
    }
    memcpy(&recipe, hd.recipe, sizeof(recipe));
    card_no = hd.no;
    G.gen.play_fps = hd.play_fps;
    G.gen.play_fps_out = hd.play_fps_out;
    G.gen.boomerang = hd.boomerang;
    G.gen.detail = W > 40;
    make_card_art(&seed, seed_art, W, H);
    have_cur = false;
    kept = true;
    dirty = false;
    play_t0 = plat_ms();
    return nframes > 0;
}

static bool save_gif(char *out_name) {
    if (!nframes || !plat_fs_ok()) return false;
    char path[48] = "gifs/";
    char nm[32];
    pick_name(&seed, nm);
    for (char *c = nm; *c; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9'))) *c = '_';
    strcat(path, nm);
    strcat(path, "-");
    char a[8];
    num_str(a, 10000 + card_no);
    strcat(path, a + 1);
    strcat(path, ".gif");
    if (!gif_begin(path, CARD_W, CARD_H)) return false;
    int fi = MAX(2, S()->play_fps), fo = MAX(2, S()->play_fps_out);
    int seq = (S()->boomerang && nframes > 2) ? 2 * nframes - 2 : nframes;
    if (nframes == 1) seq = 12;
    for (int k = 0; k < seq; k++) {
        int f = nframes == 1 ? 0 : (k < nframes ? k : 2 * nframes - 2 - k);
        bool out = k >= nframes;
        CardInfo ci;
        card_info(&ci, f);
        card_render(card_buf, &ci, frame_ptr(f), W, H, k * 5);
        if (!gif_frame(card_buf, 100 / (out ? fo : fi))) return false;
        progress = (k + 1) * 256 / seq;
        // keep the screens alive while encoding
        Surf *s = &S_BOT;
        s_rect(s, 0, 180, SCREEN_W, 12, COL(2, 1, 5));
        s_rect(s, 4, 184, (SCREEN_W - 8) * progress / 256, 4, COL8(138, 255, 234));
        plat_present(false);
    }
    gif_end();
    progress = -1;
    strcpy(out_name, path);
    return true;
}

// ---------------------------------------------------------------- picking cards
static int pick_count(void) {
    int n = G.nfus;
    for (int i = 1; i <= NUM_DREAMLINGS; i++) n += G.caught[i] ? 1 : 0;
    return n;
}
static bool pick_at(int i, Pick *p) {
    memset(p, 0, sizeof(*p));
    if (i < G.nfus) { p->kind = 2; p->fus = (u16)(G.nfus - 1 - i); return true; }
    i -= G.nfus;
    for (int num = 1; num <= NUM_DREAMLINGS; num++)
        if (G.caught[num] && i-- == 0) {
            p->kind = 1;
            p->num = (u8)num;
            for (int t = 3; t >= 0; t--)
                if (G.tiers[num] & (1 << t)) { p->tier = (u8)t; break; }
            return true;
        }
    return false;
}

void generator_open(int area_model) {
    pool_alloc();
    model_idx = CLAMP(area_model, 0, dn_model_count - 1);
    model = dn_models[model_idx];
    page = P_MAIN;
    work = W_IDLE;
    progress = -1;
    floated = true;   // whatever was left from last time already floated out
    Pick p;
    if (pick_count() > 0) pick_at(0, &p);
    else {
        // nothing caught yet: dream a wild dreamling of this town
        const Area *A = area_for_map(G.map);
        p.kind = 1;
        p.num = (u8)(A->first + rnd_range(A->last - A->first + 1));
        p.tier = 0;
        p.fus = 0;
    }
    set_seed(&p);
    char nm[32];
    pick_name(&seed, nm);
    strcpy(status, nm);
    strcat(status, " popped out of your collection. Press Zoom!");
    if (S()->book && !G.gen.pad_hint_seen) {
        strcpy(status, "Book mode: hold your DS sideways. Tap Rotate (or press R) to switch back.");
        G.gen.pad_hint_seen = 1;
    }
    if (!pool) strcpy(status, "Not enough memory for dream frames on this console.");
    gallery_load();
    set_mode(MODE_GENERATOR);
}

// ---------------------------------------------------------------- settings rows
enum { R_PRESET, R_PATH, R_FRAMES, R_ZOOM, R_STEPS, R_LR, R_RANGE, R_SEG, R_SHARP, R_LAYERS, R_FOCUS, R_SIZE, R_VIEW, R_HAND, R_COUNT };
static const char *const ROW_LABEL[R_COUNT] = {"Preset", "Zoom path", "Frames/run", "Zoom factor", "Steps/frame", "Learning rate", "Move range",
                                               "Frames/side", "Keep sharp", "Layers", "Focus", "Dream size", "View", "Hand"};

static void row_value(int r, char *o) {
    const GenSettings *s = S();
    switch (r) {
    case R_PRESET: strcpy(o, preset_idx >= 0 ? PRESETS[preset_idx].name : "Custom"); break;
    case R_PATH: strcpy(o, PATH_NAME[s->path]); break;
    case R_FRAMES: num_str(o, s->frames); break;
    case R_ZOOM: fx_str(o, s->zoom, 10000, 4); break;
    case R_STEPS: num_str(o, s->steps); break;
    case R_LR: fx_str(o, s->lr, 1000, 3); break;
    case R_RANGE: fx_str(o, s->range, 1000, 3); break;
    case R_SEG: num_str(o, s->seg); break;
    case R_SHARP: fx_str(o, s->sharpen, 100, 2); break;
    case R_LAYERS: {
        o[0] = 0;
        for (int h = 0; h < 3; h++)
            if (s->layers & (1 << h)) { if (o[0]) strcat(o, "+"); strcat(o, h == 0 ? "lo" : h == 1 ? "mid" : "hi"); }
        break;
    }
    case R_FOCUS: {
        if ((s->groups & 15) == 15 || !(s->groups & 15)) { strcpy(o, "All"); break; }
        o[0] = 0;
        int h = (s->layers & DN_HI) ? 2 : (s->layers & DN_MID) ? 1 : 0;
        for (int g = 0; g < 4; g++)
            if (s->groups & (1 << g)) { if (o[0]) strcat(o, ","); strcat(o, model->group_label[h][g]); }
        break;
    }
    case R_SIZE: strcpy(o, s->detail ? "80x100 slow" : "40x50 fast"); break;
    case R_VIEW: strcpy(o, s->book ? "Book" : "Upright"); break;
    case R_HAND: strcpy(o, s->left_handed ? "Left" : "Right"); break;
    }
}

static void row_change(int r, int d) {
    GenSettings *s = &G.gen;
    if (r != R_PRESET && r < R_SIZE) preset_idx = -1;
    switch (r) {
    case R_PRESET: {
        preset_idx = preset_idx < 0 ? (d > 0 ? 0 : NPRESETS - 1) : (preset_idx + d + NPRESETS) % NPRESETS;
        const Preset *p = &PRESETS[preset_idx];
        s->path = p->path; s->frames = p->frames; s->zoom = p->zoom; s->steps = p->steps; s->lr = p->lr;
        s->range = p->range; s->seg = p->seg; s->start_steps = p->start; s->sharpen = p->sharpen; s->layers = p->layers; s->groups = 15;
        break;
    }
    case R_PATH: s->path = (u8)((s->path + 3 + d) % 3); break;
    case R_FRAMES: s->frames = (u8)CLAMP(s->frames + d * (s->frames >= 20 ? 4 : 1), 1, 200); break;
    case R_ZOOM: s->zoom = (u16)CLAMP(s->zoom + d * (s->zoom >= 9900 ? 10 : 50), 9000, 10000); break;
    case R_STEPS: s->steps = (u8)CLAMP(s->steps + d, 0, 30); break;
    case R_LR: s->lr = (u16)CLAMP(s->lr + d * (s->lr >= 20 ? 5 : 1), 1, 200); break;
    case R_RANGE: s->range = (u8)CLAMP(s->range + d * 10, 0, 250); break;
    case R_SEG: s->seg = (u8)CLAMP(s->seg + d, 1, 60); break;
    case R_SHARP: s->sharpen = (u8)CLAMP(s->sharpen + d * 5, 0, 100); break;
    case R_LAYERS: s->layers = (u8)(((s->layers - 1 + d + 7) % 7) + 1); break;  // cycles lo, mid, lo+mid, hi, ...
    case R_FOCUS: {
        static const u8 F[5] = {15, 1, 2, 4, 8};
        int i = 0;
        for (int k = 0; k < 5; k++)
            if (F[k] == (s->groups & 15)) i = k;
        s->groups = F[(i + d + 5) % 5];
        break;
    }
    case R_SIZE:
        s->detail ^= 1;
        if (!nframes && work == W_IDLE) set_seed(&seed);
        break;
    case R_VIEW: s->book ^= 1; break;
    case R_HAND: s->left_handed ^= 1; break;
    }
    save_dirty = true;
}

// ---------------------------------------------------------------- drawing
static void estimate_str(char *o) {
    o[0] = 0;
    if (!ms_per_mac_q16) return;
    u32 cost = dn_step_cost(model, H, W);
    u32 ms = (u32)(((u64)ms_per_mac_q16 * cost / 1000) >> 16) * MAX(1, S()->steps);
    u32 run = ms * S()->frames / 1000;
    char a[12];
    strcpy(o, "~");
    fx_str(a, (int)(ms / 100), 10, 1);
    strcat(o, a);
    strcat(o, "s/frame, run ");
    if (run < 120) { num_str(a, (int)run); strcat(o, a); strcat(o, "s"); }
    else { num_str(a, (int)(run / 60)); strcat(o, a); strcat(o, " min"); }
}

static void draw_top(void) {
    int fi = (work == W_IDLE) ? play_frame() : -1;
    const u16 *art;
    if (work != W_IDLE) art = live_art;
    else if (fi >= 0) art = frame_ptr(fi);
    else art = seed_art;
    CardInfo ci;
    card_info(&ci, fi >= 0 ? fi : MAX(0, nframes - 1));
    if (work == W_START) { ci.nbullets = 1; strcpy(ci.bullets[0], status); }
    card_render(card_buf, &ci, art, W, H, (int)frame_no);
    if (S()->book) card_draw_book(&S_TOP, card_buf, S()->left_handed);
    else {
        Surf *s = &S_TOP;
        s_fill(s, COL(2, 1, 4));
        card_draw_small(s, card_buf, 3, 2);
        int x = 144;
        s_text_sh(s, "Dream", x, 6, COL8(201, 160, 255), 1);
        s_text_sh(s, "Generator", x, 17, COL8(201, 160, 255), 1);
        mini_text(s, model->name, x, 32, COL8(138, 255, 234));
        mini_text(s, "DS DREAM MODEL", x, 40, COL(18, 16, 24));
        for (int b = 0; b < ci.nbullets; b++) s_text_wrap(s, ci.bullets[b], x, 56 + b * 30, SCREEN_W - x - 4, 9, COL(24, 22, 28), 3);
    }
}

static bool rotate_req;   // applied at the start of the next update, between layouts
static void toggle_view(void) {
    G.gen.book ^= 1;
    save_dirty = true;
    strcpy(status, G.gen.book ? "Book mode: hold your DS sideways. Tap Rotate (or press R) to switch back." : "Upright view. Tap Rotate (or press R) for book mode.");
    // wipe both screens so nothing from the old layout lingers
    s_fill(&S_TOP, COL(2, 1, 4));
    s_fill(&S_BOT, COL(2, 1, 4));
}

static Surf panel;
static void panel_begin(void) {
    if (S()->book) { panel = (Surf){portrait, 192, 256}; ui_set_touch_xform(S()->left_handed ? 2 : 1); }
    else { panel = S_BOT; ui_set_touch_xform(0); }
}
static void panel_end(void) {
    if (S()->book) blit_portrait(&S_BOT, portrait, S()->left_handed);
}

static void draw_progress(Surf *s, int y) {
    s_rect(s, 6, y, s->w - 12, 4, COL(4, 2, 8));
    if (progress >= 0) s_rect(s, 6, y, (s->w - 12) * progress / 256, 4, COL8(138, 255, 234));
}

static int stepper(Surf *s, int x, int y, int w, const char *label, int value, int *out_d) {
    // [-] value [+]
    char v[12];
    num_str(v, value);
    *out_d = 0;
    if (ui_button(s, (Rect){(s16)x, (s16)y, 20, 20}, "-", COL(8, 6, 14), true)) *out_d = -1;
    if (ui_button(s, (Rect){(s16)(x + w - 20), (s16)y, 20, 20}, "+", COL(8, 6, 14), true)) *out_d = 1;
    s_text_c(s, v, x + w / 2, y + 2, COL(31, 31, 31), 1);
    mini_text(s, label, x + w / 2 - mini_w(label) / 2, y + 13, COL(16, 14, 22));
    return *out_d;
}

static void main_page(void) {
    Surf *s = &panel;
    int pw = s->w;
    bool compact = s->h < 220;
    draw_bg_pattern(s, (int)frame_no, COL(2, 1, 5), COL(5, 2, 10));
    s_text_sh(s, "Dream Generator", 6, 4, COL8(201, 160, 255), 1);
    if (ui_button(s, (Rect){(s16)(pw - 56), 2, 52, 13}, "Rotate", COL8(255, 224, 138), true)) rotate_req = true;
    char m[40];
    strcpy(m, area_for_map(G.map)->name);
    strcat(m, " / ");
    strcat(m, model->name);
    mini_text(s, m, 6, 16, COL8(138, 255, 234));
    s_rect_blend(s, 4, 24, pw - 8, 22, COL(1, 0, 3), 150);
    s_text_wrap(s, status, 7, 26, pw - 14, 10, COL(26, 24, 30), 2);
    draw_progress(s, 48);
    int y = 56;
    int bh = compact ? 26 : 32;
    bool busy = work != W_IDLE;
    const char *primary = busy ? "Stop" : (nframes >= max_frames() ? "Card is full" : "Zoom!");
    char zl[32];
    if (!busy && nframes < max_frames()) { strcpy(zl, "Zoom ("); strcat(zl, PATH_NAME[S()->path]); strcat(zl, ")"); primary = zl; }
    if (ui_button(s, (Rect){6, (s16)y, (s16)(pw - 12), (s16)bh}, primary, busy ? COL8(255, 143, 191) : COL8(138, 255, 234),
                  busy || (seed.kind && nframes < max_frames()))) {
        if (busy) { stop_req = true; if (work == W_START) work = W_IDLE; strcpy(status, "Stopped."); progress = -1; if (nframes == 0 && have_cur) push_frame(); }
        else { if (!nframes) start_dream(); start_zoom(); }
    }
    y += bh + 4;
    int bw = (pw - 12 - 8) / 3, h2 = compact ? 22 : 26;
    if (ui_button(s, (Rect){6, (s16)y, (s16)bw, (s16)h2}, "Card", COL8(201, 160, 255), !busy)) { page = P_PICK; pick_scroll = 0; }
    if (ui_button(s, (Rect){(s16)(10 + bw), (s16)y, (s16)bw, (s16)h2}, "Settings", COL8(201, 160, 255), !busy)) { page = P_SETTINGS; settings_scroll = 0; }
    if (ui_button(s, (Rect){(s16)(14 + 2 * bw), (s16)y, (s16)bw, (s16)h2}, kept && !dirty ? "Kept" : "Keep", COL8(255, 224, 138), !busy && nframes && plat_fs_ok() && (dirty || !kept))) {
        strcpy(status, keep_card() ? "Kept! It's in the Gallery (and on the SD card)." : "Couldn't save to the SD card.");
    }
    y += h2 + 4;
    if (ui_button(s, (Rect){6, (s16)y, (s16)bw, (s16)h2}, "Gallery", COL8(201, 160, 255), !busy && plat_fs_ok())) { gallery_load(); page = P_GALLERY; gallery_scroll = 0; }
    if (ui_button(s, (Rect){(s16)(10 + bw), (s16)y, (s16)bw, (s16)h2}, "GIF", COL8(255, 143, 191), !busy && nframes && plat_fs_ok())) {
        char name[48];
        strcpy(status, "Saving GIF to the SD card...");
        if (save_gif(name)) { strcpy(status, "Saved "); strcat(status, name); }
        else strcpy(status, "Couldn't write the GIF to the SD card.");
    }
    if (ui_button(s, (Rect){(s16)(14 + 2 * bw), (s16)y, (s16)bw, (s16)h2}, "Town", COL(9, 7, 14), !busy)) {
        leave_generator();
        return;
    }
    y += h2 + 6;
    // playback controls
    int d;
    int sw = compact ? (pw - 12 - 8) / 3 : (pw - 12 - 4) / 2;
    stepper(s, 6, y, sw, "IN FPS", S()->play_fps, &d);
    if (d) { G.gen.play_fps = (u8)CLAMP(S()->play_fps + d * 2, 2, 30); play_t0 = plat_ms(); if (kept) dirty = true; }
    stepper(s, 10 + sw, y, sw, "OUT FPS", S()->play_fps_out, &d);
    if (d) { G.gen.play_fps_out = (u8)CLAMP(S()->play_fps_out + d * 2, 2, 30); play_t0 = plat_ms(); if (kept) dirty = true; }
    int zx = compact ? 14 + 2 * sw : 6, zy = compact ? y : y + 26, zw = compact ? sw : pw - 12;
    if (ui_button(s, (Rect){(s16)zx, (s16)zy, (s16)zw, 20}, S()->boomerang ? "\x07 Zoomerang on" : "Zoomerang off", S()->boomerang ? COL8(138, 255, 234) : COL(8, 6, 14), true)) {
        G.gen.boomerang ^= 1;
        play_t0 = plat_ms();
        if (kept) dirty = true;
    }
    char est[48];
    estimate_str(est);
    mini_text(s, est, 6, s->h - 9, COL(16, 14, 22));
    char fr[24];
    num_str(fr, nframes);
    strcat(fr, "/");
    char a[8];
    num_str(a, max_frames());
    strcat(fr, a);
    strcat(fr, " FRAMES");
    mini_text(s, fr, pw - 6 - mini_w(fr), s->h - 9, COL(16, 14, 22));
}

static void settings_page(void) {
    Surf *s = &panel;
    int pw = s->w;
    s_fill(s, COL(2, 1, 5));
    s_text_sh(s, "Dream settings", 6, 4, COL8(201, 160, 255), 1);
    if (ui_button(s, (Rect){(s16)(pw - 50), 2, 46, 18}, "Done", COL8(138, 255, 234), true)) { page = P_MAIN; save_dirty = true; return; }
    int rowh = 21, y0 = 24, rows = (s->h - y0 - 24) / rowh;
    for (int i = 0; i < rows; i++) {
        int r = settings_scroll + i;
        if (r >= R_COUNT) break;
        int y = y0 + i * rowh;
        s_rect(s, 4, y, pw - 8, rowh - 2, i & 1 ? COL(3, 2, 6) : COL(4, 2, 8));
        s_text(s, ROW_LABEL[r], 8, y + 5, COL(24, 22, 28), 1);
        char v[32];
        row_value(r, v);
        int vx = pw - 50 - text_w(v, 1);
        s_text(s, v, MAX(78, vx), y + 5, COL8(255, 224, 138), 1);
        if (ui_button(s, (Rect){(s16)(pw - 46), (s16)(y + 1), 20, (s16)(rowh - 4)}, "-", COL(9, 6, 15), true)) row_change(r, -1);
        if (ui_button(s, (Rect){(s16)(pw - 24), (s16)(y + 1), 20, (s16)(rowh - 4)}, "+", COL(9, 6, 15), true)) row_change(r, 1);
    }
    int by = s->h - 22;
    if (ui_button(s, (Rect){6, (s16)by, 60, 18}, "\x06 Up", COL(8, 6, 14), settings_scroll > 0)) settings_scroll = MAX(0, settings_scroll - rows);
    if (ui_button(s, (Rect){70, (s16)by, 60, 18}, "Down \x05", COL(8, 6, 14), settings_scroll + rows < R_COUNT)) settings_scroll = MIN(R_COUNT - 1, settings_scroll + rows);
    char e[48];
    estimate_str(e);
    mini_text(s, e, 136, by + 7, COL(16, 14, 22));
}

static void pick_page(void) {
    Surf *s = &panel;
    int pw = s->w;
    s_fill(s, COL(2, 1, 5));
    s_text_sh(s, "Pick a card", 6, 4, COL8(201, 160, 255), 1);
    if (ui_button(s, (Rect){(s16)(pw - 54), 2, 50, 18}, "Close", COL(9, 7, 14), true)) { page = P_MAIN; return; }
    int n = pick_count();
    int cols = pw / 46, cell = pw / cols, rows = (s->h - 50) / cell;
    if (!n) {
        s_text_wrap(s, "No dreamlings caught yet. Spin the slots in the water first! (A wild one is loaded for now.)", 8, 40, pw - 16, 10, COL(24, 22, 28), 5);
        return;
    }
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++) {
            int i = (pick_scroll + r) * cols + c;
            if (i >= n) break;
            Pick p;
            pick_at(i, &p);
            int x = c * cell + 2, y = 24 + r * cell;
            bool selp = p.kind == seed.kind && (p.kind == 1 ? p.num == seed.num : p.fus == seed.fus);
            s_rect(s, x, y, cell - 4, cell - 4, COL(4, 2, 8));
            draw_pick(s, &p, x + (cell - 4) / 2, y + (cell - 4) / 2, 32);
            s_box(s, x, y, cell - 4, cell - 4, selp ? COL8(138, 255, 234) : (p.kind == 2 ? COL8(255, 143, 191) : COL(9, 7, 14)));
            if (ui_tapped((Rect){(s16)x, (s16)y, (s16)(cell - 4), (s16)(cell - 4)})) {
                if (!(dirty && nframes) || selp) {
                    set_seed(&p);
                    char nm[32];
                    pick_name(&p, nm);
                    strcpy(status, nm);
                    strcat(status, " is ready. Press Zoom!");
                    page = P_MAIN;
                } else {
                    set_seed(&p);
                    strcpy(status, "Switched card (the old dream wasn't kept).");
                    page = P_MAIN;
                }
                return;
            }
        }
    int rows_total = (n + cols - 1) / cols, by = s->h - 22;
    if (ui_button(s, (Rect){6, (s16)by, 60, 18}, "\x06 Up", COL(8, 6, 14), pick_scroll > 0)) pick_scroll = MAX(0, pick_scroll - rows);
    if (ui_button(s, (Rect){70, (s16)by, 60, 18}, "Down \x05", COL(8, 6, 14), pick_scroll + rows < rows_total)) pick_scroll += rows;
    mini_text(s, "FUSIONS FIRST", 136, by + 7, COL(16, 14, 22));
}

static void gallery_page(void) {
    Surf *s = &panel;
    int pw = s->w;
    s_fill(s, COL(2, 1, 5));
    s_text_sh(s, "Gallery (SD card)", 6, 4, COL8(201, 160, 255), 1);
    if (ui_button(s, (Rect){(s16)(pw - 54), 2, 50, 18}, "Close", COL(9, 7, 14), true)) { page = P_MAIN; return; }
    if (!ngallery) {
        s_text_wrap(s, "No kept cards yet. Dream one and press Keep.", 8, 40, pw - 16, 10, COL(24, 22, 28), 4);
        return;
    }
    int rowh = 22, rows = (s->h - 50) / rowh;
    for (int i = 0; i < rows; i++) {
        int gi = ngallery - 1 - (gallery_scroll + i);
        if (gi < 0) break;
        GalleryEntry *e = &gallery[gi];
        int y = 24 + i * rowh;
        char line[48], a[8];
        strcpy(line, "No.");
        num_str(a, 10000 + e->no);
        strcat(line, a + 1);
        strcat(line, " ");
        strcat(line, e->name);
        bool tapped = ui_button(s, (Rect){4, (s16)y, (s16)(pw - 8), (s16)(rowh - 3)}, "", COL(5, 3, 10), true);
        s_text(s, line, 10, y + 5, TIER_COLOR[MIN(e->rarity, 2)], 1);
        char fr[16];
        num_str(fr, e->nframes);
        strcat(fr, "F ");
        strcat(fr, dn_models[MIN(e->model, dn_model_count - 1)]->short_name);
        mini_text(s, fr, pw - 10 - mini_w(fr), y + 7, COL(18, 16, 24));
        if (tapped) {
            strcpy(status, load_card(gi) ? "Loaded from the gallery. Zoom to keep going." : "Couldn't read that card from the SD card.");
            page = P_MAIN;
            return;
        }
    }
    int by = s->h - 22;
    if (ui_button(s, (Rect){6, (s16)by, 60, 18}, "\x06 Up", COL(8, 6, 14), gallery_scroll > 0)) gallery_scroll = MAX(0, gallery_scroll - rows);
    if (ui_button(s, (Rect){70, (s16)by, 60, 18}, "Down \x05", COL(8, 6, 14), gallery_scroll + rows < ngallery)) gallery_scroll += rows;
}

// physical buttons, rotated for book mode so "up" is up the way you hold it
static u32 rotate_keys(u32 k) {
    if (!S()->book) return k;
    u32 o = k & ~(u32)(K_UP | K_DOWN | K_LEFT | K_RIGHT);
    if (!S()->left_handed) {
        if (k & K_RIGHT) o |= K_UP;
        if (k & K_LEFT) o |= K_DOWN;
        if (k & K_UP) o |= K_LEFT;
        if (k & K_DOWN) o |= K_RIGHT;
    } else {
        if (k & K_LEFT) o |= K_UP;
        if (k & K_RIGHT) o |= K_DOWN;
        if (k & K_DOWN) o |= K_LEFT;
        if (k & K_UP) o |= K_RIGHT;
    }
    return o;
}

void generator_update(void) {
    u32 down = rotate_keys(plat_keys_down());
    if (((down & K_R) && page == P_MAIN) || rotate_req) { rotate_req = false; toggle_view(); }
    // one unit of dreaming per update keeps the buttons responsive
    if (work == W_START) start_work_step();
    else if (work == W_ZOOM) zoom_work_step();
    if (page == P_MAIN) {
        if (down & K_A) {
            if (work != W_IDLE) { stop_req = true; if (work == W_START) work = W_IDLE; }
            else { if (!nframes) start_dream(); start_zoom(); }
        }
        if ((down & K_B) && work == W_IDLE) {
            leave_generator();
            return;
        }
        if ((down & K_SELECT) && work == W_IDLE) page = P_SETTINGS;
        if ((down & K_Y) && work == W_IDLE) page = P_PICK;
        if (down & K_UP) { G.gen.play_fps = (u8)MIN(30, S()->play_fps + 2); }
        if (down & K_DOWN) { G.gen.play_fps = (u8)MAX(2, S()->play_fps - 2); }
        if (down & K_X) { G.gen.boomerang ^= 1; play_t0 = plat_ms(); }
    } else if (down & K_B) page = P_MAIN;
    draw_top();
    panel_begin();
    switch (page) {
    case P_MAIN: main_page(); break;
    case P_SETTINGS: settings_page(); break;
    case P_PICK: pick_page(); break;
    case P_GALLERY: gallery_page(); break;
    }
    if (game_mode == MODE_GENERATOR) panel_end();
}
