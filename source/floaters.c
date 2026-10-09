// Floating dreams: every dream you make in a Dream Generator drifts around the
// town it was made in, as the dreamling's outline with the dream playing inside.
// They live in RAM only (they're gone when the DS is switched off).
#include "game.h"

#define FL_N 12          // floaters in all towns
#define FL_PER_TOWN 4    // newest 4 per town
#define FL_S 46          // 44x44 creature + 1px outline all round
#define FL_F 8           // dream frames kept per floater

enum { M_OUT = 0, M_IN = 1, M_EDGE = 2 };

typedef struct {
    u8 used, map, nf, rarity;
    u32 born;
    s32 x, y, tx, ty;            // world position / wander target, Q8 pixels
    char name[24];
    u8 mask[FL_S * FL_S];
    u16 px[FL_F][FL_S * FL_S];
} Floater;

static Floater fl[FL_N];
static u32 born_counter;
static s16 bx0, by0, bx1, by1;   // mask bounding box while building

static void new_target(Floater *f, int map_w, int map_h) {
    f->tx = (s32)(16 + rnd_range(MAX(1, map_w - 32))) << 8;
    f->ty = (s32)(16 + rnd_range(MAX(1, map_h - 40))) << 8;
}

int floater_begin(int map, const Pick *p, int x, int y) {
    // reuse a free slot; past 4 in this town (or no room) replace the oldest
    int in_town = 0, oldest_town = -1, free_slot = -1, oldest_any = 0;
    for (int i = 0; i < FL_N; i++) {
        if (!fl[i].used) { if (free_slot < 0) free_slot = i; continue; }
        if (fl[i].born < fl[oldest_any].born || !fl[oldest_any].used) oldest_any = i;
        if (fl[i].map != map) continue;
        in_town++;
        if (oldest_town < 0 || fl[i].born < fl[oldest_town].born) oldest_town = i;
    }
    int s = in_town >= FL_PER_TOWN ? oldest_town : (free_slot >= 0 ? free_slot : oldest_any);
    Floater *f = &fl[s];
    memset(f->mask, 0, sizeof(f->mask));
    f->used = 0;   // not drawn until floater_end
    f->map = (u8)map;
    f->nf = 0;
    f->rarity = (u8)pick_rarity(p);
    f->born = ++born_counter;
    f->x = f->tx = (s32)x << 8;
    f->y = f->ty = (s32)y << 8;
    pick_name(p, f->name);
    f->name[sizeof(f->name) - 1] = 0;

    // the creature's shape: draw it over black and over white; where both
    // agree it's (mostly) opaque
    static u16 a[FL_S * FL_S], b[FL_S * FL_S];
    Surf sa = {a, FL_S, FL_S}, sb = {b, FL_S, FL_S};
    s_fill(&sa, COL(0, 0, 0));
    s_fill(&sb, COL(31, 31, 31));
    draw_pick(&sa, p, FL_S / 2, FL_S / 2, FL_S - 2);
    draw_pick(&sb, p, FL_S / 2, FL_S / 2, FL_S - 2);
    for (int i = 0; i < FL_S * FL_S; i++) {
        int d = MAX(MAX(C_R(b[i]) - C_R(a[i]), C_G(b[i]) - C_G(a[i])), C_B(b[i]) - C_B(a[i]));
        if (d <= 20) f->mask[i] = M_IN;
    }
    // outline: empty pixels touching the shape
    bx0 = by0 = FL_S;
    bx1 = by1 = -1;
    for (int y = 0; y < FL_S; y++)
        for (int x = 0; x < FL_S; x++) {
            int i = y * FL_S + x;
            if (f->mask[i] == M_IN) {
                bx0 = MIN(bx0, x); by0 = MIN(by0, y); bx1 = MAX(bx1, x); by1 = MAX(by1, y);
                continue;
            }
            bool edge = (x > 0 && f->mask[i - 1] == M_IN) || (x < FL_S - 1 && f->mask[i + 1] == M_IN) ||
                        (y > 0 && f->mask[i - FL_S] == M_IN) || (y < FL_S - 1 && f->mask[i + FL_S] == M_IN);
            if (edge) f->mask[i] = M_EDGE;
        }
    if (bx1 < bx0) { bx0 = by0 = 0; bx1 = by1 = FL_S - 1; }
    return s;
}

// one dream frame (w x h) poured into the creature's shape, cover-fit
void floater_frame(int s, const u16 *src, int w, int h) {
    Floater *f = &fl[s];
    if (f->nf >= FL_F) return;
    u16 *dst = f->px[f->nf++];
    int bw = bx1 - bx0 + 1, bh = by1 - by0 + 1;
    // source pixels per mask pixel (Q8): the smaller ratio, so the dream covers the shape
    s32 k = MIN(w * 256 / bw, h * 256 / bh);
    s32 cx = w * 128, cy = h * 128;   // Q8 centre of the dream
    for (int y = 0; y < FL_S; y++)
        for (int x = 0; x < FL_S; x++) {
            int i = y * FL_S + x;
            if (f->mask[i] != M_IN) { dst[i] = 0; continue; }
            s32 sx = (cx + (2 * (x - bx0) - bw + 1) * k / 2) >> 8;
            s32 sy = (cy + (2 * (y - by0) - bh + 1) * k / 2) >> 8;
            dst[i] = src[CLAMP(sy, 0, h - 1) * w + CLAMP(sx, 0, w - 1)];
        }
}

void floater_end(int s) {
    if (fl[s].nf) fl[s].used = 1;
}

int floaters_in_town(int map) {
    int n = 0;
    for (int i = 0; i < FL_N; i++) n += fl[i].used && fl[i].map == map;
    return n;
}

void floaters_draw(Surf *s, int map, int camx, int camy, int map_w, int map_h, int pcx, int pcy) {
    u32 t = frame_no;
    for (int i = 0; i < FL_N; i++) {
        Floater *f = &fl[i];
        if (!f->used || f->map != map) continue;
        // drift slowly toward a wander point, then pick another
        s32 dx = f->tx - f->x, dy = f->ty - f->y;
        if (IABS(dx) < 512 && IABS(dy) < 512) new_target(f, map_w, map_h);
        else {
            s32 d = MAX(IABS(dx), IABS(dy));
            f->x += (s32)((s64)dx * 90 / d);   // ~0.35 px per frame
            f->y += (s32)((s64)dy * 90 / d);
        }
        int bob = isin((int)(t * 9 + i * 170) & 1023) * 3 >> 12;
        int sx = (f->x >> 8) - camx - FL_S / 2, sy = (f->y >> 8) - camy - FL_S / 2 - 10 + bob;
        if (sx > SCREEN_W || sy > SCREEN_H || sx + FL_S < 0 || sy + FL_S + 16 < 0) continue;
        // soft shadow on the ground
        s_rect_blend(s, sx + 13, sy + FL_S + 6 - bob, 20, 2, COL(0, 0, 0), 60);
        s_rect_blend(s, sx + 16, sy + FL_S + 8 - bob, 14, 1, COL(0, 0, 0), 40);
        // zoomerang through the kept frames, ~6 fps
        int nf = f->nf, ph = (int)((t / 10 + i * 3) % (u32)MAX(1, 2 * nf - 2));
        int fi = nf <= 1 ? 0 : (ph < nf ? ph : 2 * nf - 2 - ph);
        const u16 *src = f->px[fi];
        u16 edge = blend(TIER_COLOR[MIN(f->rarity, 2)], COL(31, 31, 31), 128 + (isin((int)(t * 12 + i * 99) & 1023) >> 5));
        for (int y = 0; y < FL_S; y++) {
            int yy = sy + y;
            if ((unsigned)yy >= SCREEN_H) continue;
            u16 *row = s->px + yy * SCREEN_W;
            const u8 *m = f->mask + y * FL_S;
            for (int x = 0; x < FL_S; x++) {
                int xx = sx + x;
                if ((unsigned)xx >= SCREEN_W || m[x] == M_OUT) continue;
                row[xx] = m[x] == M_IN ? src[y * FL_S + x] : edge;
            }
        }
        // its name when you walk up to it
        int fx = (f->x >> 8) - camx, fy = (f->y >> 8) - camy;
        if (IABS(fx - pcx) < 30 && IABS(fy - 10 - pcy) < 30) {
            int w = mini_w(f->name);
            s_rect_blend(s, fx - w / 2 - 2, sy - 9, w + 4, 8, COL(1, 0, 3), 170);
            mini_text(s, f->name, fx - w / 2, sy - 8, edge);
        }
    }
}
