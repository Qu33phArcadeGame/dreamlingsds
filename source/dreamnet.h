// DS dream engine: a tiny CNN distilled from Inception v1/v2/v3, run in
// fixed point, with DeepDream gradient ascent on its "lo / mid / hi" layers.
#pragma once
#include "common.h"

#define DN_MAX_W 80
#define DN_MAX_H 100
#define DN_NCONV 5
#define DN_NHEAD 3
#define DN_IMG_ONE 4096          // image values: -4096..4096 = -1..1 (Q12)

typedef struct {
    const s16 *w;      // [cout][kp]: 3x3xcin filter, zero padded to an even length kp
    const s16 *wt;     // [3][3][cin][cout]: the same weights, transposed for the backward pass
    const s32 *b;      // bias in accumulator units
    u8 cin, cout, stride;
    s8 shift;          // accumulator -> output activation
    u8 qw;             // weight fraction bits
    u16 kp;            // padded filter length
} DNConv;

typedef struct {
    const char *name;        // "Inception v1"
    const char *short_name;  // "v1"
    DNConv conv[DN_NCONV];
    u8 head_src[DN_NHEAD];   // which conv each head reads (lo=1, mid=2, hi=4)
    const s32 *seed[DN_NHEAD]; // [4 groups][channels of head_src], float * 2^DN_SEED_Q
    const char *head_label[DN_NHEAD];
    const char *group_label[DN_NHEAD][4];
} DNModel;
#define DN_SEED_Q 20

enum { DN_LO = 1, DN_MID = 2, DN_HI = 4 };

typedef struct {
    u8 layers;   // DN_LO | DN_MID | DN_HI
    u8 groups;   // bit per group (0..3); 0 or 15 = whole layer
} DNObjective;

extern const DNModel *const dn_models[];
extern const int dn_model_count;

// One gradient-ascent step on img (H x W x 3, Q12) in place.
// lr_q12: step size in image units (4096 = 1.0). jitter: max random shift in pixels.
// smooth: passes of 3x3 blur on the gradient (1 is a good default).
void dn_step(const DNModel *m, s16 *img, int H, int W, int lr_q12, DNObjective obj, int jitter, int smooth);

// Image helpers (all H x W x 3, Q12)
void dn_resize(const s16 *src, int sh, int sw, s16 *dst, int dh, int dw);
// Zoom in: crop (zf_q16 / 65536) of the image around its centre + (offx, offy)
// pixels (Q8), scaled back up to H x W.
void dn_zoom(const s16 *src, s16 *dst, int H, int W, int zf_q16, int offx_q8, int offy_q8);
void dn_sharpen(s16 *img, int H, int W, int amount_q8);
void dn_from_rgb15(const u16 *src, s16 *dst, int n);
void dn_to_rgb15(const s16 *src, u16 *dst, int n);
void dn_add_noise(s16 *img, int n, int amp);
// rough multiply-accumulate count for one step (for time estimates)
u32 dn_step_cost(const DNModel *m, int H, int W);
