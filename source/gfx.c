#include "gfx.h"
#include "assets.h"
#include "platform.h"

Surf S_TOP = {fb_top, SCREEN_W, SCREEN_H};
Surf S_BOT = {fb_bot, SCREEN_W, SCREEN_H};

const u16 TIER_COLOR[4] = {COL8(255, 85, 85), COL8(93, 255, 143), COL8(93, 180, 255), COL8(196, 107, 255)};

u16 hsv(int h, int s, int v) {
    h %= 360;
    if (h < 0) h += 360;
    int region = h / 60, rem = (h - region * 60) * 255 / 60;
    int p = (v * (255 - s)) >> 8, q = (v * (255 - ((s * rem) >> 8))) >> 8, t = (v * (255 - ((s * (255 - rem)) >> 8))) >> 8;
    int r, g, b;
    switch (region) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    return COL8(r, g, b);
}

// invert + hue-rotate + saturate, same look as the website's rarity filter
static const s16 TINT[3][12] = {
    {-251, 242, -247, -24, -313, 82, 228, -146, -338, 256, 256, 256},
    {244, -183, -317, -161, -140, 45, 117, -610, 237, 256, 256, 256},
    {166, -604, 181, -94, -50, -112, -319, -257, 320, 256, 256, 256},
};
u16 tier_tint(u16 c, int tier) {
    if (tier <= 0 || !c) return c;
    const s16 *m = TINT[tier - 1];
    int r = C_R(c), g = C_G(c), b = C_B(c);
    int nr = (m[9] * 31 + m[0] * r + m[1] * g + m[2] * b) >> 8;
    int ng = (m[10] * 31 + m[3] * r + m[4] * g + m[5] * b) >> 8;
    int nb = (m[11] * 31 + m[6] * r + m[7] * g + m[8] * b) >> 8;
    return COL(CLAMP(nr, 0, 31), CLAMP(ng, 0, 31), CLAMP(nb, 0, 31));
}

// ---------------------------------------------------------------- shapes
void s_fill(Surf *s, u16 c) {
    int n = s->w * s->h;
    u32 c2 = c | ((u32)c << 16);
    u32 *p = (u32 *)s->px;
    for (int i = 0; i < n / 2; i++) p[i] = c2;
}
void s_px(Surf *s, int x, int y, u16 c) {
    if ((unsigned)x < (unsigned)s->w && (unsigned)y < (unsigned)s->h) s->px[y * s->w + x] = c;
}
void s_rect(Surf *s, int x, int y, int w, int h, u16 c) {
    int x0 = MAX(x, 0), y0 = MAX(y, 0), x1 = MIN(x + w, s->w), y1 = MIN(y + h, s->h);
    for (int yy = y0; yy < y1; yy++) {
        u16 *p = s->px + yy * s->w;
        for (int xx = x0; xx < x1; xx++) p[xx] = c;
    }
}
void s_rect_blend(Surf *s, int x, int y, int w, int h, u16 c, int alpha) {
    int x0 = MAX(x, 0), y0 = MAX(y, 0), x1 = MIN(x + w, s->w), y1 = MIN(y + h, s->h);
    for (int yy = y0; yy < y1; yy++) {
        u16 *p = s->px + yy * s->w;
        for (int xx = x0; xx < x1; xx++) p[xx] = blend(p[xx], c, alpha);
    }
}
void s_box(Surf *s, int x, int y, int w, int h, u16 c) {
    s_rect(s, x, y, w, 1, c);
    s_rect(s, x, y + h - 1, w, 1, c);
    s_rect(s, x, y, 1, h, c);
    s_rect(s, x + w - 1, y, 1, h, c);
}
void s_panel(Surf *s, int x, int y, int w, int h, u16 fill, u16 edge) {
    s_rect(s, x + 2, y, w - 4, h, edge);
    s_rect(s, x + 1, y + 1, w - 2, h - 2, edge);
    s_rect(s, x, y + 2, w, h - 4, edge);
    s_rect(s, x + 2, y + 1, w - 4, h - 2, fill);
    s_rect(s, x + 1, y + 2, w - 2, h - 4, fill);
}
void s_line(Surf *s, int x0, int y0, int x1, int y1, u16 c) {
    int dx = IABS(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -IABS(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (int n = 0; n < 2048; n++) {
        s_px(s, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
void s_circle(Surf *s, int cx, int cy, int r, u16 c, int alpha) {
    if (r <= 0) return;
    for (int y = -r; y <= r; y++) {
        int yy = cy + y;
        if (yy < 0 || yy >= s->h) continue;
        int span = 0;
        while ((span + 1) * (span + 1) + y * y <= r * r) span++;
        int x0 = MAX(cx - span, 0), x1 = MIN(cx + span, s->w - 1);
        u16 *p = s->px + yy * s->w;
        if (alpha >= 256)
            for (int x = x0; x <= x1; x++) p[x] = c;
        else
            for (int x = x0; x <= x1; x++) p[x] = blend(p[x], c, alpha);
    }
}
void s_ring(Surf *s, int cx, int cy, int r, u16 c) {
    for (int a = 0; a < 1024; a += 4) s_px(s, cx + (icos(a) * r >> 12), cy + (isin(a) * r >> 12), c);
}

// ---------------------------------------------------------------- sprites
void s_blit(Surf *s, const u16 *src, int sw, int sh, int dx, int dy) {
    for (int y = 0; y < sh; y++) {
        int yy = dy + y;
        if (yy < 0 || yy >= s->h) continue;
        u16 *p = s->px + yy * s->w;
        const u16 *q = src + y * sw;
        for (int x = 0; x < sw; x++) {
            int xx = dx + x;
            if (q[x] && (unsigned)xx < (unsigned)s->w) p[xx] = q[x];
        }
    }
}
void s_blit_ex(Surf *s, const u16 *src, int sw, int sh, int dx, int dy, int dw, int dh, int tier, int alpha, bool flip) {
    if (dw <= 0 || dh <= 0) return;
    int stepx = (sw << 16) / dw, stepy = (sh << 16) / dh;
    int y0 = MAX(0, -dy), y1 = MIN(dh, s->h - dy);
    int x0 = MAX(0, -dx), x1 = MIN(dw, s->w - dx);
    for (int y = y0; y < y1; y++) {
        const u16 *row = src + ((y * stepy) >> 16) * sw;
        u16 *p = s->px + (dy + y) * s->w + dx;
        for (int x = x0; x < x1; x++) {
            int sx = (x * stepx) >> 16;
            if (flip) sx = sw - 1 - sx;
            u16 c = row[sx];
            if (!c) continue;
            if (tier) c = tier_tint(c, tier);
            p[x] = alpha >= 256 ? c : blend(p[x], c, alpha);
        }
    }
}
void s_blit_pal(Surf *s, const u8 *idx, const u16 *pal, int sw, int sh, int dx, int dy, int dw, int dh, int tier, int alpha) {
    if (dw <= 0 || dh <= 0) return;
    u16 tpal[256];
    if (tier) {
        tpal[0] = 0;
        for (int i = 1; i < 256; i++) tpal[i] = tier_tint(pal[i], tier);
        pal = tpal;
    }
    int stepx = (sw << 16) / dw, stepy = (sh << 16) / dh;
    int y0 = MAX(0, -dy), y1 = MIN(dh, s->h - dy);
    int x0 = MAX(0, -dx), x1 = MIN(dw, s->w - dx);
    for (int y = y0; y < y1; y++) {
        const u8 *row = idx + ((y * stepy) >> 16) * sw;
        u16 *p = s->px + (dy + y) * s->w + dx;
        for (int x = x0; x < x1; x++) {
            u8 i = row[(x * stepx) >> 16];
            if (!i) continue;
            p[x] = alpha >= 256 ? pal[i] : blend(p[x], pal[i], alpha);
        }
    }
}

void s_copy_scaled(Surf *s, const u16 *src, int sw, int sh, int dx, int dy, int dw, int dh) {
    if (dw <= 0 || dh <= 0) return;
    int stepx = (sw << 16) / dw, stepy = (sh << 16) / dh;
    int y0 = MAX(0, -dy), y1 = MIN(dh, s->h - dy);
    int x0 = MAX(0, -dx), x1 = MIN(dw, s->w - dx);
    for (int y = y0; y < y1; y++) {
        const u16 *row = src + ((y * stepy) >> 16) * sw;
        u16 *p = s->px + (dy + y) * s->w + dx;
        for (int x = x0; x < x1; x++) p[x] = row[(x * stepx) >> 16] | 0x8000;
    }
}

// ---------------------------------------------------------------- text
static int glyph_index(unsigned char ch) {
    if (ch >= 32 && ch < 127) return ch - 32;
    if (ch >= 1 && ch <= 7) return 95 + ch - 1;
    return '?' - 32;
}
int text_w(const char *t, int scale) {
    int w = 0;
    for (; *t; t++) {
        if (*t == '\n') break;
        w += (font_width[glyph_index((unsigned char)*t)] + 1) * scale;
    }
    return w ? w - scale : 0;
}
int s_text(Surf *s, const char *t, int x, int y, u16 c, int scale) {
    int x0 = x;
    for (; *t; t++) {
        if (*t == '\n') { y += (FONT_H + 1) * scale; x = x0; continue; }
        int gi = glyph_index((unsigned char)*t);
        const u8 *rows = font_bits + gi * 9;
        int gw = font_width[gi];
        for (int r = 0; r < 9; r++) {
            u8 bits = rows[r];
            if (!bits) continue;
            for (int col = 0; col < gw; col++)
                if (bits & (1 << col)) {
                    if (scale == 1) s_px(s, x + col, y + r, c);
                    else s_rect(s, x + col * scale, y + r * scale, scale, scale, c);
                }
        }
        x += (gw + 1) * scale;
    }
    return x - x0;
}
int s_text_sh(Surf *s, const char *t, int x, int y, u16 c, int scale) {
    s_text(s, t, x + scale, y + scale, COL(1, 0, 3), scale);
    return s_text(s, t, x, y, c, scale);
}
void s_text_c(Surf *s, const char *t, int cx, int y, u16 c, int scale) {
    s_text(s, t, cx - text_w(t, scale) / 2, y, c, scale);
}
int s_text_wrap(Surf *s, const char *t, int x, int y, int maxw, int lineh, u16 c, int maxlines) {
    char line[160];
    int lines = 0;
    while (*t && lines < maxlines) {
        int n = 0, last_space = -1;
        line[0] = 0;
        while (t[n] && t[n] != '\n' && n < 158) {
            line[n] = t[n];
            line[n + 1] = 0;
            if (t[n] == ' ') last_space = n;
            if (text_w(line, 1) > maxw) {
                if (last_space > 0) n = last_space;
                break;
            }
            n++;
        }
        line[n] = 0;
        if (c) s_text(s, line, x, y + lines * lineh, c, 1);
        lines++;
        t += n;
        while (*t == ' ' || *t == '\n') t++;
    }
    return lines;
}
void fit_text(char *out, const char *t, int maxw, int scale) {
    strncpy(out, t, 95);
    out[95] = 0;
    if (text_w(out, scale) <= maxw) return;
    int n = (int)strlen(out);
    while (n > 1) {
        out[--n] = 0;
        char tmp[100];
        strcpy(tmp, out);
        strcat(tmp, "...");
        if (text_w(tmp, scale) <= maxw) { strcpy(out, tmp); return; }
    }
}
