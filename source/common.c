// Names, creature drawing, card art, small UI helpers, area table
#include "game.h"
#include "dreamnet.h"

// ---------------------------------------------------------------- areas
// Dream Core = Inception v1, Waking Woods = v2, Area 3 = v3 (same as the website).
static const Area AREAS[] = {
    // Dream Core fuses 2 per spin; Tri-beings start in the Waking Woods
    {"DREAM CORE", 1, 11, 0, 0, 8, 20, COL(8, 3, 16), 2},
    {"WAKING WOODS", 12, 22, 1, 5, 5, 22, COL(3, 10, 6), 3},
    {"AREA 3", 23, 33, 2, 5, 11, 6, COL(12, 4, 8), 3},
};
static const Area AREA_HALL = {"HALLWAY", 1, 11, 0, 0, -1, -1, COL(6, 4, 12), 2};
const Area *area_for_map(int map) {
    switch (map) {
    case MAP_MAP: return &AREAS[0];
    case MAP_ROOM1: { static Area r; r = AREAS[0]; r.gen_x = r.gen_y = -1; return &r; }
    case MAP_HALL1: return &AREA_HALL;
    case MAP_WOODS: return &AREAS[1];
    case MAP_HALL2: { static Area h; h = AREAS[1]; h.name = "HALLWAY"; h.gen_x = h.gen_y = -1; return &h; }
    case MAP_AREA3: return &AREAS[2];
    }
    return &AREAS[0];
}

// ---------------------------------------------------------------- names (identical to the website)
static const char *const SYL1[24] = {"oo", "vex", "mo", "glo", "zu", "fer", "wis", "kra", "ny", "sha", "bl", "gru",
                                     "um", "ith", "qua", "zel", "pho", "dra", "ee", "aur", "tza", "mir", "ob", "syl"};
static const char *const SYL2[24] = {"na", "tl", "phi", "ox", "ba", "ren", "du", "mo", "sk", "va", "lo", "th",
                                     "ex", "ir", "um", "al", "oz", "en", "ix", "ar", "is", "oon", "el", "yx"};
static s64 dl_s;
static void dl_rng(s64 seed) {
    dl_s = (seed * 2654435761LL) % 2147483647LL;
    if (dl_s <= 0) dl_s += 2147483646LL;
}
static s64 dl_next(void) {
    dl_s = (dl_s * 16807LL) % 2147483647LL;
    return dl_s - 1; // r() = this / 2147483646
}
static int dl_pick(int n) { return (int)((dl_next() * n) / 2147483646LL); }

void dreamling_name(int num, char *out) {
    dl_rng(num * 131 + 5);
    const char *a = SYL1[dl_pick(24)];
    const char *b = SYL2[dl_pick(24)];
    strcpy(out, a);
    strcat(out, b);
    if (dl_next() * 100 < 45LL * 2147483646LL) strcat(out, SYL1[dl_pick(24)]);
    if (out[0] >= 'a' && out[0] <= 'z') out[0] -= 32;
}
void fusion_name(const Fusion *f, char *out) {
    char nm[64] = "";
    for (int i = 0; i < f->n; i++) {
        char nn[24];
        dreamling_name(f->comp[i], nn);
        int len = (int)strlen(nn);
        if (i == 0) {
            int half = (len + 1) / 2;
            memcpy(nm, nn, half);
            nm[half] = 0;
        } else {
            char *p = nn + len / 2;
            for (char *q = p; *q; q++)
                if (*q >= 'A' && *q <= 'Z') *q += 32;
            strncat(nm, p, sizeof(nm) - strlen(nm) - 1);
        }
    }
    nm[20] = 0;
    if (nm[0] >= 'a' && nm[0] <= 'z') nm[0] -= 32;
    strcpy(out, nm);
}
const char *fusion_label(const Fusion *f) {
    return f->n >= 4 ? "Deepdreamling" : f->n == 3 ? "Tri-being" : f->n == 2 ? "Fusion" : "Dreamling";
}
int fusion_best_tier(const Fusion *f) {
    int t = 0;
    for (int i = 0; i < f->n; i++) t = MAX(t, f->tier[i]);
    return t;
}

// ---------------------------------------------------------------- creatures
void draw_dreamling(Surf *s, int num, int tier, int cx, int cy, int size, int alpha) {
    if (num < 1 || num > NUM_DREAMLINGS) return;
    if (size > 40)
        s_blit_pal(s, dreamling64_idx + (num - 1) * 64 * 64, dreamling64_pal + (num - 1) * 256, 64, 64,
                   cx - size / 2, cy - size / 2, size, size, tier, alpha);
    else
        s_blit_pal(s, dreamling32_idx + (num - 1) * 32 * 32, dreamling32_pal + (num - 1) * 256, 32, 32,
                   cx - size / 2, cy - size / 2, size, size, tier, alpha);
}
void draw_fusion(Surf *s, const Fusion *f, int cx, int cy, int size) {
    int op = f->n <= 2 ? 158 : (f->n == 3 ? 128 : 108);
    for (int i = 0; i < f->n; i++) draw_dreamling(s, f->comp[i], f->tier[i], cx, cy, size, i == 0 ? 256 : op);
}
void draw_pick(Surf *s, const Pick *p, int cx, int cy, int size) {
    if (p->kind == 1) draw_dreamling(s, p->num, p->tier, cx, cy, size, 256);
    else if (p->kind == 2 && p->fus < G.nfus) draw_fusion(s, &G.fus[p->fus], cx, cy, size);
}
void draw_tier_dots(Surf *s, const u8 *tiers, int n, int cx, int cy) {
    int x = cx - (n - 1) * 4;
    for (int i = 0; i < n; i++, x += 8) {
        s_circle(s, x, cy, 3, COL(0, 0, 2), 256);
        s_circle(s, x, cy, 2, TIER_COLOR[tiers[i] & 3], 256);
    }
}

void pick_name(const Pick *p, char *out) {
    if (p->kind == 2 && p->fus < G.nfus) fusion_name(&G.fus[p->fus], out);
    else if (p->kind == 1) dreamling_name(p->num, out);
    else strcpy(out, "Nobody");
}
void pick_detail(const Pick *p, char *out) {
    if (p->kind == 2 && p->fus < G.nfus) {
        const Fusion *f = &G.fus[p->fus];
        strcpy(out, fusion_label(f));
        char n[12];
        n[0] = ' ';
        n[1] = '#';
        int v = f->power, k = 2;
        char d[8];
        int nd = 0;
        do { d[nd++] = (char)('0' + v % 10); v /= 10; } while (v);
        while (nd) n[k++] = d[--nd];
        n[k] = 0;
        strcat(out, n);
    } else if (p->kind == 1) {
        strcpy(out, "Dreamling #");
        char d[8];
        int nd = 0, v = p->num;
        do { d[nd++] = (char)('0' + v % 10); v /= 10; } while (v);
        int k = (int)strlen(out);
        while (nd) out[k++] = d[--nd];
        out[k] = 0;
    } else out[0] = 0;
}
int pick_rarity(const Pick *p) {
    if (p->kind == 2 && p->fus < G.nfus) {
        const Fusion *f = &G.fus[p->fus];
        int r = f->rare == 2 ? 2 : (f->rare == 1 ? 1 : 0);
        int bt = fusion_best_tier(f);
        return MAX(r, bt >= 2 ? 2 : bt);
    }
    if (p->kind == 1) return p->tier >= 2 ? 2 : p->tier;
    return 0;
}

void make_card_art(const Pick *p, u16 *out, int w, int h) {
    Surf s = {out, w, h};
    s_fill(&s, COL(1, 1, 4));
    u8 comp[MAX_COMP], tier[MAX_COMP];
    int n = 0;
    if (p->kind == 1) { comp[0] = p->num; tier[0] = p->tier; n = 1; }
    else if (p->kind == 2 && p->fus < G.nfus) {
        const Fusion *f = &G.fus[p->fus];
        n = f->n;
        memcpy(comp, f->comp, n);
        memcpy(tier, f->tier, n);
    }
    // dream photos layered together
    for (int j = 0; j < n; j++) {
        const u8 *ph = card_photo_idx + (comp[j] - 1) * CARD_PHOTO_W * CARD_PHOTO_H;
        const u16 *pal = card_photo_pal + (comp[j] - 1) * 256;
        int stepx = (CARD_PHOTO_W << 16) / w, stepy = (CARD_PHOTO_H << 16) / h;
        for (int y = 0; y < h; y++) {
            const u8 *row = ph + ((y * stepy) >> 16) * CARD_PHOTO_W;
            u16 *o = out + y * w;
            for (int x = 0; x < w; x++) {
                u16 c = pal[row[(x * stepx) >> 16]] | 0x8000;
                o[x] = j == 0 ? c : blend(o[x], c, 154);
            }
        }
    }
    s_rect_blend(&s, 0, 0, w, h, COL(1, 1, 5), 51);
    // the creature(s) in front
    int size = MIN(w, h) * 86 / 100;
    int op = n <= 2 ? 158 : (n == 3 ? 128 : 108);
    for (int j = 0; j < n; j++) draw_dreamling(&s, comp[j], tier[j], w / 2, h / 2, size, j == 0 ? 256 : op);
}

// ---------------------------------------------------------------- tiny 3x5 font (dense text)
static const u8 MINI[96][5] = {
    [' ' - 32] = {0, 0, 0, 0, 0}, ['!' - 32] = {2, 2, 2, 0, 2}, ['.' - 32] = {0, 0, 0, 0, 2},
    [',' - 32] = {0, 0, 0, 2, 4}, [':' - 32] = {0, 2, 0, 2, 0}, ['-' - 32] = {0, 0, 7, 0, 0},
    ['+' - 32] = {0, 2, 7, 2, 0}, ['/' - 32] = {1, 1, 2, 4, 4}, ['(' - 32] = {1, 2, 2, 2, 1},
    [')' - 32] = {4, 2, 2, 2, 4}, ['%' - 32] = {5, 1, 2, 4, 5}, ['=' - 32] = {0, 7, 0, 7, 0},
    ['>' - 32] = {4, 2, 1, 2, 4}, ['<' - 32] = {1, 2, 4, 2, 1}, ['#' - 32] = {5, 7, 5, 7, 5},
    ['\'' - 32] = {2, 2, 0, 0, 0}, ['?' - 32] = {6, 1, 2, 0, 2}, ['*' - 32] = {5, 2, 7, 2, 5},
    ['0' - 32] = {7, 5, 5, 5, 7}, ['1' - 32] = {2, 6, 2, 2, 7}, ['2' - 32] = {7, 1, 7, 4, 7},
    ['3' - 32] = {7, 1, 7, 1, 7}, ['4' - 32] = {5, 5, 7, 1, 1}, ['5' - 32] = {7, 4, 7, 1, 7},
    ['6' - 32] = {7, 4, 7, 5, 7}, ['7' - 32] = {7, 1, 2, 2, 2}, ['8' - 32] = {7, 5, 7, 5, 7},
    ['9' - 32] = {7, 5, 7, 1, 7},
    ['A' - 32] = {2, 5, 7, 5, 5}, ['B' - 32] = {6, 5, 6, 5, 6}, ['C' - 32] = {3, 4, 4, 4, 3},
    ['D' - 32] = {6, 5, 5, 5, 6}, ['E' - 32] = {7, 4, 6, 4, 7}, ['F' - 32] = {7, 4, 6, 4, 4},
    ['G' - 32] = {3, 4, 5, 5, 3}, ['H' - 32] = {5, 5, 7, 5, 5}, ['I' - 32] = {7, 2, 2, 2, 7},
    ['J' - 32] = {1, 1, 1, 5, 2}, ['K' - 32] = {5, 5, 6, 5, 5}, ['L' - 32] = {4, 4, 4, 4, 7},
    ['M' - 32] = {5, 7, 7, 5, 5}, ['N' - 32] = {6, 5, 5, 5, 5}, ['O' - 32] = {2, 5, 5, 5, 2},
    ['P' - 32] = {6, 5, 6, 4, 4}, ['Q' - 32] = {2, 5, 5, 6, 3}, ['R' - 32] = {6, 5, 6, 5, 5},
    ['S' - 32] = {3, 4, 2, 1, 6}, ['T' - 32] = {7, 2, 2, 2, 2}, ['U' - 32] = {5, 5, 5, 5, 7},
    ['V' - 32] = {5, 5, 5, 5, 2}, ['W' - 32] = {5, 5, 7, 7, 5}, ['X' - 32] = {5, 5, 2, 5, 5},
    ['Y' - 32] = {5, 5, 2, 2, 2}, ['Z' - 32] = {7, 1, 2, 4, 7},
};
void mini_text(Surf *s, const char *t, int x, int y, u16 c) {
    for (; *t; t++, x += 4) {
        int ch = (unsigned char)*t;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch == 7) { // zoomerang symbol
            s_px(s, x, y + 2, c); s_px(s, x + 1, y + 1, c); s_px(s, x + 1, y + 3, c); s_px(s, x + 2, y + 2, c);
            continue;
        }
        if (ch < 32 || ch >= 128) continue;
        const u8 *g = MINI[ch - 32];
        for (int r = 0; r < 5; r++)
            for (int col = 0; col < 3; col++)
                if (g[r] & (4 >> col)) s_px(s, x + col, y + r, c);
    }
}
int mini_w(const char *t) { int n = (int)strlen(t); return n ? n * 4 - 1 : 0; }

// ---------------------------------------------------------------- UI
int touch_x, touch_y;
bool touch_down, touch_held;
static int touch_mode;
static bool prev_held;
void ui_set_touch_xform(int mode) { touch_mode = mode; }
void ui_begin_frame(void) {
    u32 held = plat_keys_held();
    touch_held = (held & K_TOUCH) != 0;
    touch_down = touch_held && !prev_held;
    prev_held = touch_held;
    if (touch_held) {
        int tx, ty;
        plat_touch(&tx, &ty);
        if (touch_mode == 1) { touch_x = ty; touch_y = 255 - tx; }
        else if (touch_mode == 2) { touch_x = 191 - ty; touch_y = tx; }
        else { touch_x = tx; touch_y = ty; }
    }
}
bool ui_tapped(Rect r) {
    return touch_down && touch_x >= r.x && touch_x < r.x + r.w && touch_y >= r.y && touch_y < r.y + r.h;
}
bool ui_button(Surf *s, Rect r, const char *label, u16 fill, bool enabled) {
    bool pressed = enabled && touch_held && touch_x >= r.x && touch_x < r.x + r.w && touch_y >= r.y && touch_y < r.y + r.h;
    u16 f = enabled ? fill : blend(fill, COL(4, 3, 8), 170);
    if (pressed) f = blend(f, COL(31, 31, 31), 70);
    s_panel(s, r.x, r.y, r.w, r.h, f, enabled ? blend(f, COL(0, 0, 0), 150) : COL(5, 4, 9));
    s_rect(s, r.x + 2, r.y + 1, r.w - 4, 1, blend(f, COL(31, 31, 31), 90));
    u16 tc = enabled ? (C_R(f) + C_G(f) + C_B(f) > 50 ? COL(2, 1, 5) : COL(30, 28, 31)) : COL(12, 10, 16);
    s_text_c(s, label, r.x + r.w / 2, r.y + (r.h - 7) / 2, tc, 1);
    return enabled && ui_tapped(r);
}
void draw_bg_pattern(Surf *s, int t, u16 a, u16 b) {
    // soft moving waves, computed on 4x4 blocks (cheap enough for the DS)
    for (int by = 0; by < s->h; by += 4) {
        for (int bx = 0; bx < s->w; bx += 4) {
            int v = isin((bx * 6 + t) & 1023) + icos((by * 5 - t / 2) & 1023) + isin(((bx + by) * 3 + t * 2) & 1023);
            u16 c = blend(a, b, CLAMP(128 + (v >> 7), 0, 256));
            int h = MIN(4, s->h - by), w = MIN(4, s->w - bx);
            for (int y = 0; y < h; y++) {
                u16 *p = s->px + (by + y) * s->w + bx;
                for (int x = 0; x < w; x++) p[x] = c;
            }
        }
    }
}
