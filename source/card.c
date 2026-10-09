#include "card.h"

u16 card_shared[CARD_W * CARD_H];
u16 portrait_shared[192 * 256];

static const struct { int h0, span; } FOIL[3] = {{330, 70}, {85, 110}, {0, 360}};
static const char *const RARITY_LABEL[3] = {"\x04 Common", "\x02 Rare", "\x03 Holo"};
static const u16 RARITY_COL[3] = {COL8(255, 85, 85), COL8(93, 255, 143), COL8(93, 180, 255)};

void card_render(u16 *dst, const CardInfo *ci, const u16 *art, int aw, int ah, int t) {
    Surf s = {dst, CARD_W, CARD_H};
    int r = MIN(ci->rarity, 2);
    // 1) foil palette for this moment (diagonal gradient)
    u16 lut[64];
    int shift = (t * 2) % 360;
    for (int k = 0; k < 64; k++) {
        int hue;
        if (FOIL[r].span >= 360) hue = shift + k * 360 / 63;
        else hue = FOIL[r].h0 + ((isin((shift * 1024 / 360 + k * 92) & 1023) + 4096) * FOIL[r].span >> 13);
        lut[k] = hsv(hue, r == 2 ? 230 : 215, 255 * 6 / 10 + 80);
    }
    // sheen band moving along the other diagonal
    int band = ((t * 2) % 600) - 150; // in (x - y + 252) units
    // 2) foil + art + frame in one pass
    const int stepx = (aw << 16) / ART_W, stepy = (ah << 16) / ART_H;
    for (int y = 0; y < CARD_H; y++) {
        u16 *o = dst + y * CARD_W;
        const u16 *fr = card_frame + y * CARD_W;
        bool in_art_row = y >= ART_Y && y < ART_Y + ART_H;
        const u16 *arow = in_art_row && art ? art + (((y - ART_Y) * stepy) >> 16) * aw : 0;
        for (int x = 0; x < CARD_W; x++) {
            u16 f = fr[x];
            if (f) { o[x] = f; continue; }
            if (in_art_row && x >= ART_X && x < ART_X + ART_W) {
                o[x] = arow ? (arow[((x - ART_X) * stepx) >> 16] | 0x8000) : COL(1, 0, 2);
                continue;
            }
            u16 c = lut[((x + y) * (63 * 65536 / (CARD_W + CARD_H))) >> 16];
            int d = (x - y + CARD_H) - band;
            if (d > -24 && d < 24) c = blend(c, COL(31, 31, 31), (24 - IABS(d)) * 140 / 24);
            o[x] = c;
        }
    }
    // 3) holo shimmer over the art for rare cards
    if (r > 0 && art) {
        int p = ((t * 3) % 520) - 120;
        for (int y = 0; y < ART_H; y++) {
            u16 *o = dst + (ART_Y + y) * CARD_W + ART_X;
            for (int x = 0; x < ART_W; x++) {
                int d = (x + y) - p;
                if (d > -20 && d < 20) o[x] = blend(o[x], COL(31, 31, 31), (20 - IABS(d)) * (r == 2 ? 70 : 40) / 20);
            }
        }
    }
    // 4) frame-count badge
    if (ci->frames > 1) {
        char b[16];
        int n = ci->frames, k = 0;
        char d[6];
        int nd = 0;
        do { d[nd++] = (char)('0' + n % 10); n /= 10; } while (n);
        while (nd) b[k++] = d[--nd];
        strcpy(b + k, " frames");
        int w = mini_w(b) + 6;
        s_rect_blend(&s, ART_X + 3, ART_Y + 3, w, 9, COL(1, 0, 3), 190);
        mini_text(&s, b, ART_X + 6, ART_Y + 5, COL8(138, 255, 234));
    }
    // 5) recipe bullets along the bottom of the art
    if (ci->nbullets) {
        char lines[12][44];
        u8 first[12];
        int nl = 0;
        for (int b = 0; b < ci->nbullets && nl < 10; b++) {
            const char *txt = ci->bullets[b];
            bool f = true;
            while (*txt && nl < 10) {
                int maxc = 36, n = (int)strlen(txt);
                if (n > maxc) {
                    n = maxc;
                    while (n > 10 && txt[n] != ' ') n--;
                }
                memcpy(lines[nl], txt, n);
                lines[nl][n] = 0;
                first[nl] = f;
                f = false;
                nl++;
                txt += n;
                while (*txt == ' ') txt++;
            }
        }
        int lh = 7, bh = nl * lh + 4, by = ART_Y + ART_H - bh;
        for (int y = by - 8; y < ART_Y + ART_H; y++) {
            int a = y < by ? (y - (by - 8)) * 20 : 200;
            s_rect_blend(&s, ART_X, y, ART_W, 1, COL(1, 0, 3), MIN(a, 210));
        }
        for (int i = 0; i < nl; i++) {
            int y = by + 2 + i * lh;
            if (first[i]) s_rect(&s, ART_X + 3, y + 2, 2, 2, COL8(138, 255, 234));
            mini_text(&s, lines[i], ART_X + 8, y, i == 0 ? COL(31, 31, 31) : COL8(217, 204, 242));
        }
    }
    // 6) name plate (30..149 x 6..18) and info plate (30..149 x 229..247)
    const char *rl = RARITY_LABEL[r];
    int rlw = text_w(rl, 1);
    s_text(&s, rl, 146 - rlw, 9, RARITY_COL[r], 1);
    char fit[96];
    fit_text(fit, ci->name, 146 - rlw - 6 - 34, 1);
    s_text(&s, fit, 34, 9, COL(31, 31, 31), 1);
    char no[16];
    if (ci->number) {
        strcpy(no, "No.");
        int n = ci->number;
        no[3] = (char)('0' + (n / 1000) % 10); no[4] = (char)('0' + (n / 100) % 10);
        no[5] = (char)('0' + (n / 10) % 10); no[6] = (char)('0' + n % 10); no[7] = 0;
    } else strcpy(no, "NEW");
    int nw = mini_w(no);
    mini_text(&s, no, 146 - nw, 232, COL8(138, 255, 234));
    char det[64];
    strncpy(det, ci->detail, 63);
    det[63] = 0;
    while (mini_w(det) > 146 - nw - 6 - 34 && strlen(det) > 1) det[strlen(det) - 1] = 0;
    mini_text(&s, det, 34, 232, COL8(201, 160, 255));
    char st[64];
    strncpy(st, ci->style, 63);
    st[63] = 0;
    while (mini_w(st) > 112 && strlen(st) > 1) st[strlen(st) - 1] = 0;
    mini_text(&s, st, 90 - mini_w(st) / 2, 240, COL8(185, 168, 216));
}

void card_draw_small(Surf *s, const u16 *card, int x0, int y0) {
    // 3/4 scale: take 3 of every 4 source pixels (nearest)
    for (int y = 0; y < CARD_H * 3 / 4; y++) {
        int yy = y0 + y;
        if ((unsigned)yy >= (unsigned)s->h) continue;
        const u16 *src = card + ((y * 21846) >> 14) * CARD_W;   // y * 4 / 3
        u16 *o = s->px + yy * s->w;
        for (int x = 0; x < CARD_W * 3 / 4; x++) {
            int xx = x0 + x;
            if ((unsigned)xx < (unsigned)s->w) o[xx] = src[(x * 21846) >> 14];
        }
    }
}

void blit_portrait(Surf *s, const u16 *pv, bool left_handed) {
    // portrait is 192 wide x 256 tall. Right-handed: DS turned anticlockwise,
    // so portrait (u, v) shows at screen (255 - v, u).
    for (int y = 0; y < SCREEN_H; y++) {
        u16 *o = s->px + y * SCREEN_W;
        for (int x = 0; x < SCREEN_W; x++) {
            int u, v;
            if (!left_handed) { u = y; v = 255 - x; }
            else { u = 191 - y; v = x; }
            o[x] = pv[v * 192 + u];
        }
    }
}

void card_draw_book(Surf *s, const u16 *card, bool left_handed) {
    u16 *pv = portrait_shared;
    Surf p = {pv, 192, 256};
    s_fill(&p, COL(2, 1, 4));
    for (int y = 0; y < CARD_H; y++) memcpy(pv + (y + 2) * 192 + 6, card + y * CARD_W, CARD_W * 2);
    blit_portrait(s, pv, left_handed);
}
