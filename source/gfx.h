// Software drawing on 16-bit framebuffers
#pragma once
#include "common.h"

typedef struct { u16 *px; int w, h; } Surf;

extern Surf S_TOP, S_BOT;

enum { TIER_RED = 0, TIER_GREEN = 1, TIER_BLUE = 2, TIER_PURPLE = 3 };
extern const u16 TIER_COLOR[4];

static inline u16 blend(u16 a, u16 b, int t) { // t: 0 = a, 256 = b
    int r = C_R(a) + (((C_R(b) - C_R(a)) * t) >> 8);
    int g = C_G(a) + (((C_G(b) - C_G(a)) * t) >> 8);
    int bl = C_B(a) + (((C_B(b) - C_B(a)) * t) >> 8);
    return COL(r, g, bl);
}
static inline u16 col_add(u16 a, int r, int g, int b) {
    return COL(CLAMP(C_R(a) + r, 0, 31), CLAMP(C_G(a) + g, 0, 31), CLAMP(C_B(a) + b, 0, 31));
}
u16 hsv(int h, int s, int v); // h 0..359, s/v 0..255
u16 tier_tint(u16 c, int tier);

void s_fill(Surf *s, u16 c);
void s_px(Surf *s, int x, int y, u16 c);
void s_rect(Surf *s, int x, int y, int w, int h, u16 c);
void s_rect_blend(Surf *s, int x, int y, int w, int h, u16 c, int alpha);
void s_box(Surf *s, int x, int y, int w, int h, u16 c); // outline
void s_panel(Surf *s, int x, int y, int w, int h, u16 fill, u16 edge); // rounded panel
void s_line(Surf *s, int x0, int y0, int x1, int y1, u16 c);
void s_circle(Surf *s, int cx, int cy, int r, u16 c, int alpha);
void s_ring(Surf *s, int cx, int cy, int r, u16 c);

// Sprites: 0 pixels are transparent.
void s_blit(Surf *s, const u16 *src, int sw, int sh, int dx, int dy);
// Scaled sprite with optional tier tint, alpha (0..256) and horizontal flip.
void s_blit_ex(Surf *s, const u16 *src, int sw, int sh, int dx, int dy, int dw, int dh,
               int tier, int alpha, bool flip);
// Paletted sprite (index 0 = clear), scaled, optional tier tint / alpha.
void s_blit_pal(Surf *s, const u8 *idx, const u16 *pal, int sw, int sh, int dx, int dy, int dw, int dh, int tier, int alpha);
// Opaque copy of a region, nearest-neighbour scaled.
void s_copy_scaled(Surf *s, const u16 *src, int sw, int sh, int dx, int dy, int dw, int dh);

// Text: 5x7 pixel font (lowercase has descenders). scale 1 or 2.
int text_w(const char *t, int scale);
int s_text(Surf *s, const char *t, int x, int y, u16 c, int scale);
int s_text_sh(Surf *s, const char *t, int x, int y, u16 c, int scale); // with drop shadow
void s_text_c(Surf *s, const char *t, int cx, int y, u16 c, int scale); // centred
// Word-wrapped text; returns the number of lines drawn (or that would be drawn if c == 0).
int s_text_wrap(Surf *s, const char *t, int x, int y, int maxw, int lineh, u16 c, int maxlines);
// Fit text into maxw by trimming with "..."
void fit_text(char *out, const char *t, int maxw, int scale);
#define FONT_H 9
