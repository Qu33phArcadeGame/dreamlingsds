// Fixed-point DeepDream for the DS. See dreamnet.h and tools/student.py
// (the float reference this mirrors).
#include "dreamnet.h"

#define MAXPIX (DN_MAX_W * DN_MAX_H)
#define GZ_BITS 10   // masked gradients are scaled to fit 10 bits before each backward conv

// activations: [0] = input (rolled), [1..5] = conv outputs
static s16 __attribute__((aligned(4))) a_in[MAXPIX * 3];
static s16 __attribute__((aligned(4))) a1[MAXPIX * 12];
static s16 __attribute__((aligned(4))) a2[(MAXPIX / 4 + 64) * 16];
static s16 __attribute__((aligned(4))) a3[(MAXPIX / 16 + 64) * 24];
static s16 __attribute__((aligned(4))) a4[(MAXPIX / 64 + 64) * 32];
static s16 __attribute__((aligned(4))) a5[(MAXPIX / 64 + 64) * 32];
static s16 *const ACT[DN_NCONV + 1] = {a_in, a1, a2, a3, a4, a5};

static s32 gacc[MAXPIX * 12];      // gradient w.r.t. a layer's output (or input)
static s32 gacc2[MAXPIX * 4];       // conv2 output (16 ch at 1/4 size) or the input (3 ch)

static inline int osz(int n, int s) { return s == 2 ? (n + 1) >> 1 : n; }   // stride 1 or 2

// ------------------------------------------------------------------ dot products
// Two 16-bit values per 32-bit load; compilers turn each line of MAC2 into the
// ARM9's single-cycle SMLABB / SMLATT multiply-accumulates.
typedef u32 __attribute__((may_alias)) u32a;
#define MAC2(x, y) acc += (s32)(s16)(x) * (s32)(s16)(y) + ((s32)(x) >> 16) * ((s32)(y) >> 16)
ITCM_FN static s32 dot16(const s16 *a, const s16 *b, int n, s32 acc) {   // n even, a and b 4-byte aligned
    const u32a *pa = (const u32a *)a, *pb = (const u32a *)b;
    int pairs = n >> 1;
    while (pairs >= 4) {
        u32 x0 = pa[0], y0 = pb[0], x1 = pa[1], y1 = pb[1];
        MAC2(x0, y0);
        MAC2(x1, y1);
        u32 x2 = pa[2], y2 = pb[2], x3 = pa[3], y3 = pb[3];
        MAC2(x2, y2);
        MAC2(x3, y3);
        pa += 4;
        pb += 4;
        pairs -= 4;
    }
    while (pairs-- > 0) {
        u32 x0 = *pa++, y0 = *pb++;
        MAC2(x0, y0);
    }
    return acc;
}

// ------------------------------------------------------------------ forward
ITCM_FN static void conv_fwd(const s16 *in, int H, int W, const DNConv *L, s16 *out) {
    const int cin = L->cin, cout = L->cout, s = L->stride, K = 9 * cin, KP = L->kp;
    const int Ho = osz(H, s), Wo = osz(W, s), sh = L->shift;
    const s32 rnd = sh > 0 ? (1 << (sh - 1)) : 0;
    s16 patch[9 * 32 + 2] __attribute__((aligned(4)));
    patch[K] = 0;   // padding slot (when K is odd)
    for (int oy = 0; oy < Ho; oy++) {
        for (int ox = 0; ox < Wo; ox++) {
            s16 *pp = patch;
            const int iy0 = oy * s - 1, ix0 = ox * s - 1;
            for (int dy = 0; dy < 3; dy++) {
                const int iy = iy0 + dy;
                for (int dx = 0; dx < 3; dx++) {
                    const int ix = ix0 + dx;
                    if ((unsigned)iy < (unsigned)H && (unsigned)ix < (unsigned)W) {
                        const s16 *src = in + (iy * W + ix) * cin;
                        for (int c = 0; c < cin; c++) pp[c] = src[c];
                    } else {
                        for (int c = 0; c < cin; c++) pp[c] = 0;
                    }
                    pp += cin;
                }
            }
            const s16 *w = L->w;
            s16 *o = out + (oy * Wo + ox) * cout;
            for (int co = 0; co < cout; co++, w += KP) {
                s32 acc = dot16(patch, w, KP, L->b[co]);
                if (acc <= 0) { o[co] = 0; continue; }
                acc = sh >= 0 ? (acc + rnd) >> sh : acc << (-sh);
                o[co] = (s16)(acc > 32767 ? 32767 : acc);
            }
        }
    }
}

// ------------------------------------------------------------------ backward
// gx (H x W x cin, s32) = conv^T(gzz). Gather form: for every input pixel,
// add up the outputs that saw it, as dot products over the output channels.
ITCM_FN static void conv_bwd(const s16 *gzz, int H, int W, const DNConv *L, s32 *gx) {
    const int cin = L->cin, cout = L->cout, s = L->stride;
    const int Ho = osz(H, s), Wo = osz(W, s);
    for (int iy = 0; iy < H; iy++) {
        for (int ix = 0; ix < W; ix++) {
            // which (tap, output) pairs touch this pixel
            const s16 *g[9];
            const s16 *wt[9];
            int n = 0;
            const int sb = s >> 1;   // stride is 1 or 2
            for (int dy = 0; dy < 3; dy++) {
                int oy = iy + 1 - dy;
                if (oy < 0 || (oy & sb)) continue;
                oy >>= sb;
                if (oy >= Ho) continue;
                for (int dx = 0; dx < 3; dx++) {
                    int ox = ix + 1 - dx;
                    if (ox < 0 || (ox & sb)) continue;
                    ox >>= sb;
                    if (ox >= Wo) continue;
                    g[n] = gzz + (oy * Wo + ox) * cout;
                    wt[n] = L->wt + (dy * 3 + dx) * cin * cout;
                    n++;
                }
            }
            s32 *dst = gx + (iy * W + ix) * cin;
            for (int c = 0; c < cin; c++) {
                s32 acc = 0;
                for (int k = 0; k < n; k++) acc = dot16(g[k], wt[k] + c * cout, cout, acc);
                dst[c] = acc;
            }
        }
    }
}

static int bits_of(u32 v) {
    int b = 0;
    while (v) { b++; v >>= 1; }
    return b;
}

// gz = scale(G) where act > 0; returns the right-shift used (may be negative)
static int mask_and_scale(const s32 *G, const s16 *act, s16 *out, int n) {
    u32 mx = 0;
    for (int i = 0; i < n; i++) {
        if (act[i] <= 0) continue;
        u32 a = (u32)(G[i] < 0 ? -G[i] : G[i]);
        if (a > mx) mx = a;
    }
    int r = bits_of(mx) - GZ_BITS;
    for (int i = 0; i < n; i++) {
        if (act[i] <= 0) { out[i] = 0; continue; }
        s32 v = r >= 0 ? (G[i] >> r) : (G[i] << (-r));
        out[i] = (s16)CLAMP(v, -1023, 1023);
    }
    return r;
}

static void roll_img(const s16 *src, s16 *dst, int H, int W, int C, int jy, int jx) {
    for (int y = 0; y < H; y++) {
        int sy = y - jy;
        sy %= H;
        if (sy < 0) sy += H;
        for (int x = 0; x < W; x++) {
            int sx = x - jx;
            sx %= W;
            if (sx < 0) sx += W;
            memcpy(dst + (y * W + x) * C, src + (sy * W + sx) * C, C * sizeof(s16));
        }
    }
}

u32 dn_step_cost(const DNModel *m, int H, int W) {
    u32 total = 0;
    int h = H, w = W;
    for (int l = 0; l < DN_NCONV; l++) {
        const DNConv *L = &m->conv[l];
        h = osz(h, L->stride);
        w = osz(w, L->stride);
        total += (u32)h * w * L->cout * 9 * L->cin;
    }
    return total * 2;
}

void dn_step(const DNModel *m, s16 *img, int H, int W, int lr_q12, DNObjective obj, int jitter, int smooth) {
    int hs[DN_NCONV + 1], ws[DN_NCONV + 1];
    hs[0] = H;
    ws[0] = W;
    int jy = jitter ? (int)(rnd() % (2 * jitter + 1)) - jitter : 0;
    int jx = jitter ? (int)(rnd() % (2 * jitter + 1)) - jitter : 0;
    roll_img(img, a_in, H, W, 3, jy, jx);
    // forward: only as deep as the deepest chosen head
    int deepest = 0;
    for (int h = 0; h < DN_NHEAD; h++)
        if (obj.layers & (1 << h)) deepest = MAX(deepest, m->head_src[h]);
    for (int l = 0; l <= deepest; l++) {
        const DNConv *L = &m->conv[l];
        conv_fwd(ACT[l], hs[l], ws[l], L, ACT[l + 1]);
        hs[l + 1] = osz(hs[l], L->stride);
        ws[l + 1] = osz(ws[l], L->stride);
    }
    // seeds per conv layer (sum of chosen heads/groups), exponent DN_SEED_Q
    u8 groups = (obj.groups & 15) ? (obj.groups & 15) : 15;
    int ngroups = 0;
    for (int g = 0; g < 4; g++) ngroups += (groups >> g) & 1;
    // backward
    // buffer A (gacc) is big enough for conv1's output; B (gacc2) for everything
    // else. Start so that conv1's output lands in A.
    s32 *G = (deepest & 1) ? gacc2 : gacc, *Gnext = (deepest & 1) ? gacc : gacc2;
    int e = DN_SEED_Q;  // true gradient = G / 2^e
    bool have = false;
    for (int l = deepest; l >= 0; l--) {
        const DNConv *L = &m->conv[l];
        const int n = hs[l + 1] * ws[l + 1] * L->cout;
        // add the seed of any chosen head that reads this layer:
        // d(mean of chosen group maps)/d(activation), per position
        for (int h = 0; h < DN_NHEAD; h++) {
            if (!(obj.layers & (1 << h)) || m->head_src[h] != l) continue;
            const int C = L->cout, npos = hs[l + 1] * ws[l + 1];
            int es = DN_SEED_Q + 16;   // exponent of seedv
            s64 sv64[32];
            s64 smax = 0;
            for (int c = 0; c < C; c++) {
                s64 sum = 0;
                for (int g = 0; g < 4; g++)
                    if (groups & (1 << g)) sum += m->seed[h][g * C + c];
                sv64[c] = (sum * 4 * 65536) / ((s64)ngroups * npos);
                s64 a = sv64[c] < 0 ? -sv64[c] : sv64[c];
                if (a > smax) smax = a;
            }
            // keep the seeds within 24 bits (adjusting the exponent to match)
            int sh = 0;
            while ((smax >> sh) >= (1 << 24)) sh++;
            es -= sh;
            s32 seedv[32];
            for (int c = 0; c < C; c++) seedv[c] = (s32)(sv64[c] >> sh);
            if (!have) {
                for (int p = 0; p < npos; p++)
                    for (int c = 0; c < C; c++) G[p * C + c] = seedv[c];
                e = es;
                have = true;
            } else {
                // bring both to the smaller exponent (right shifts only: no overflow)
                int ec = MIN(e, es);
                int sg = e - ec, ss = es - ec;
                for (int p = 0; p < npos; p++)
                    for (int c = 0; c < C; c++) {
                        s32 gv = sg >= 31 ? 0 : (G[p * C + c] >> sg);
                        s32 sv = ss >= 31 ? 0 : (seedv[c] >> ss);
                        G[p * C + c] = gv + sv;
                    }
                e = ec;
            }
        }
        if (!have) continue;
        // masked + scaled gradient, written over this layer's activations
        // (they aren't needed any more once we're past this layer)
        s16 *gz = ACT[l + 1];
        int r = mask_and_scale(G, ACT[l + 1], gz, n);
        const int nin = hs[l] * ws[l] * L->cin;
        conv_bwd(gz, hs[l], ws[l], L, Gnext);
        e = e - r + L->qw;
        // keep the exponent in a sane range for the next seed alignment
        u32 mx = 0;
        for (int i = 0; i < nin; i++) {
            u32 a = (u32)(Gnext[i] < 0 ? -Gnext[i] : Gnext[i]);
            if (a > mx) mx = a;
        }
        int b = bits_of(mx);
        if (b > 24) {
            int sh = b - 24;
            for (int i = 0; i < nin; i++) Gnext[i] >>= sh;
            e -= sh;
        }
        s32 *t = G; G = Gnext; Gnext = t;
    }
    if (!have) return;
    // G = gradient w.r.t. the (rolled) input. Smooth it a little (3x3 binomial
    // blur): at these tiny sizes this keeps the dream from turning into noise.
    const int n = H * W * 3;
    for (int pass = 0; pass < smooth; pass++) {
        s32 *T = Gnext;
        for (int y = 0; y < H; y++) {
            const int y0 = y > 0 ? y - 1 : 0, y1 = y < H - 1 ? y + 1 : H - 1;
            for (int x = 0; x < W; x++) {
                const int x0 = x > 0 ? x - 1 : 0, x1 = x < W - 1 ? x + 1 : W - 1;
                for (int c = 0; c < 3; c++) {
#define GG(yy, xx) (G[((yy) * W + (xx)) * 3 + c] >> 4)
                    T[(y * W + x) * 3 + c] = GG(y0, x0) + 2 * GG(y0, x) + GG(y0, x1) + 2 * GG(y, x0) + 4 * GG(y, x) +
                                             2 * GG(y, x1) + GG(y1, x0) + 2 * GG(y1, x) + GG(y1, x1);
#undef GG
                }
            }
        }
        Gnext = G;
        G = T;
    }
    s64 sumabs = 0;
    for (int i = 0; i < n; i++) sumabs += G[i] < 0 ? -G[i] : G[i];
    if (sumabs == 0) return;
    s64 meanabs = sumabs / n;
    if (meanabs < 1) meanabs = 1;
    s64 k = ((s64)lr_q12 << 20) / meanabs;
    for (int y = 0; y < H; y++) {
        int ry = (y + jy) % H;
        if (ry < 0) ry += H;
        for (int x = 0; x < W; x++) {
            int rx = (x + jx) % W;
            if (rx < 0) rx += W;
            const s32 *g = G + (ry * W + rx) * 3;
            s16 *p = img + (y * W + x) * 3;
            for (int c = 0; c < 3; c++) {
                s32 v = p[c] + (s32)(((s64)g[c] * k) >> 20);
                p[c] = (s16)CLAMP(v, -DN_IMG_ONE, DN_IMG_ONE);
            }
        }
    }
}

// ------------------------------------------------------------------ image helpers
void dn_resize(const s16 *src, int sh, int sw, s16 *dst, int dh, int dw) {
    // bilinear, pixel-centre aligned
    for (int y = 0; y < dh; y++) {
        s32 fy = ((2 * y + 1) * sh * 256) / (2 * dh) - 128;   // Q8
        if (fy < 0) fy = 0;
        int y0 = fy >> 8, ty = fy & 255;
        int y1 = MIN(y0 + 1, sh - 1);
        if (y0 > sh - 1) y0 = sh - 1;
        for (int x = 0; x < dw; x++) {
            s32 fx = ((2 * x + 1) * sw * 256) / (2 * dw) - 128;
            if (fx < 0) fx = 0;
            int x0 = fx >> 8, tx = fx & 255;
            int x1 = MIN(x0 + 1, sw - 1);
            if (x0 > sw - 1) x0 = sw - 1;
            const s16 *p00 = src + (y0 * sw + x0) * 3, *p01 = src + (y0 * sw + x1) * 3;
            const s16 *p10 = src + (y1 * sw + x0) * 3, *p11 = src + (y1 * sw + x1) * 3;
            s16 *o = dst + (y * dw + x) * 3;
            for (int c = 0; c < 3; c++) {
                s32 top = p00[c] * (256 - tx) + p01[c] * tx;
                s32 bot = p10[c] * (256 - tx) + p11[c] * tx;
                o[c] = (s16)((top * (256 - ty) + bot * ty) >> 16);
            }
        }
    }
}

void dn_zoom(const s16 *src, s16 *dst, int H, int W, int zf_q16, int offx_q8, int offy_q8) {
    if (zf_q16 > 65536) zf_q16 = 65536;
    if (zf_q16 < 32768) zf_q16 = 32768;
    // crop size and centre in Q8 pixels
    s32 cw = (s32)(((s64)W * 256 * zf_q16) >> 16), ch = (s32)(((s64)H * 256 * zf_q16) >> 16);
    s32 cx = W * 128 + offx_q8, cy = H * 128 + offy_q8;
    cx = CLAMP(cx, cw / 2, W * 256 - cw / 2);
    cy = CLAMP(cy, ch / 2, H * 256 - ch / 2);
    s32 x0 = cx - cw / 2, y0 = cy - ch / 2;
    static s32 fxs[DN_MAX_W];
    for (int x = 0; x < W; x++) {
        s32 fx = x0 + (s32)(((2 * x + 1) * (s64)cw) / (2 * W)) - 128;
        fxs[x] = CLAMP(fx, 0, (W - 1) * 256);
    }
    for (int y = 0; y < H; y++) {
        s32 fy = y0 + (s32)(((2 * y + 1) * (s64)ch) / (2 * H)) - 128;
        fy = CLAMP(fy, 0, (H - 1) * 256);
        int yi = fy >> 8, ty = fy & 255, yj = MIN(yi + 1, H - 1);
        for (int x = 0; x < W; x++) {
            s32 fx = fxs[x];
            int xi = fx >> 8, tx = fx & 255, xj = MIN(xi + 1, W - 1);
            const s16 *p00 = src + (yi * W + xi) * 3, *p01 = src + (yi * W + xj) * 3;
            const s16 *p10 = src + (yj * W + xi) * 3, *p11 = src + (yj * W + xj) * 3;
            s16 *o = dst + (y * W + x) * 3;
            for (int c = 0; c < 3; c++) {
                s32 top = p00[c] * (256 - tx) + p01[c] * tx;
                s32 bot = p10[c] * (256 - tx) + p11[c] * tx;
                o[c] = (s16)((top * (256 - ty) + bot * ty) >> 16);
            }
        }
    }
}

void dn_sharpen(s16 *img, int H, int W, int amount_q8) {
    if (amount_q8 <= 0) return;
    s16 *tmp = a_in;   // the input buffer is free between steps
    memcpy(tmp, img, H * W * 3 * sizeof(s16));
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            for (int c = 0; c < 3; c++) {
                s32 acc = 0;
                for (int dy = -1; dy <= 1; dy++) {
                    int yy = CLAMP(y + dy, 0, H - 1);
                    for (int dx = -1; dx <= 1; dx++) {
                        int xx = CLAMP(x + dx, 0, W - 1);
                        int wgt = (dy ? 1 : 2) * (dx ? 1 : 2);
                        acc += tmp[(yy * W + xx) * 3 + c] * wgt;
                    }
                }
                s32 v = tmp[(y * W + x) * 3 + c];
                s32 blur = acc >> 4;
                v += ((v - blur) * amount_q8 * 2) >> 8;
                img[(y * W + x) * 3 + c] = (s16)CLAMP(v, -DN_IMG_ONE, DN_IMG_ONE);
            }
}

void dn_from_rgb15(const u16 *src, s16 *dst, int n) {
    for (int i = 0; i < n; i++) {
        u16 c = src[i];
        int ch[3] = {c & 31, (c >> 5) & 31, (c >> 10) & 31};
        for (int k = 0; k < 3; k++) dst[i * 3 + k] = (s16)((ch[k] * 8192 + 15) / 31 - 4096);
    }
}
void dn_to_rgb15(const s16 *src, u16 *dst, int n) {
    for (int i = 0; i < n; i++) {
        int ch[3];
        for (int k = 0; k < 3; k++) {
            int v = src[i * 3 + k] + 4096;
            ch[k] = CLAMP((v * 31 + 4096) / 8192, 0, 31);
        }
        dst[i] = COL(ch[0], ch[1], ch[2]);
    }
}
void dn_add_noise(s16 *img, int n, int amp) {
    if (amp <= 0) return;
    for (int i = 0; i < n * 3; i++) {
        s32 v = img[i] + (s32)(rnd() % (2 * amp + 1)) - amp;
        img[i] = (s16)CLAMP(v, -DN_IMG_ONE, DN_IMG_ONE);
    }
}
