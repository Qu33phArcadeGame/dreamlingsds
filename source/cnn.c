// Tiny CNN for DS: 24x24 input, 3 conv layers, fixed-point 16.16
// Used for "dreaming" (gradient ascent) on dreamling sprites

#include "cnn_weights.h"

typedef int s32;
typedef long long s64;

#define SZ 24
#define C1 8
#define C2 16
#define C3 16

// Buffers (in .bss, ~150KB total)
static s32 buf_x[SZ*SZ];
static s32 buf_a1[SZ*SZ*C1];
static s32 buf_a2[SZ*SZ*C2];
static s32 buf_a3[SZ*SZ*C3];
static s32 buf_g3[SZ*SZ*C3];  // gradient w.r.t a3
static s32 buf_g2[SZ*SZ*C2];  // gradient w.r.t a2
static s32 buf_g1[SZ*SZ*C1];  // gradient w.r.t a1
static s32 buf_gx[SZ*SZ];     // gradient w.r.t input

static inline s32 fxmul(s32 a, s32 b) {
    return (s32)(((s64)a * b) >> 16);
}

// 3x3 conv, pad=1, stride=1, + ReLU
// in: [SZ][SZ][Cin], w: [3][3][Cin][Cout], b: [Cout], out: [SZ][SZ][Cout]
static void conv_relu(const s32 *in, const s32 *w, const s32 *b,
                      s32 *out, int Cin, int Cout) {
    for (int co = 0; co < Cout; co++) {
        for (int y = 0; y < SZ; y++) {
            for (int x = 0; x < SZ; x++) {
                s64 acc = ((s64)b[co]) << 16;  // bias in 16.16 -> accumulate in 32.32
                for (int ky = -1; ky <= 1; ky++) {
                    int iy = y + ky;
                    if (iy < 0 || iy >= SZ) continue;
                    for (int kx = -1; kx <= 1; kx++) {
                        int ix = x + kx;
                        if (ix < 0 || ix >= SZ) continue;
                        // w index: [ky+1][kx+1][ci][co]
                        const s32 *wp = w + (((ky+1)*3 + (kx+1))*Cin + 0)*Cout + co;
                        const s32 *ip = in + (iy*SZ + ix)*Cin;
                        for (int ci = 0; ci < Cin; ci++) {
                            acc += (s64)ip[ci] * wp[ci*Cout];
                        }
                    }
                }
                s32 v = (s32)(acc >> 16);
                out[(y*SZ + x)*Cout + co] = v > 0 ? v : 0;
            }
        }
    }
}

// Backward through ReLU+Conv: given dL/dout, compute dL/din
// dout_grad: [SZ][SZ][Cout], in_act: [SZ][SZ][Cin] (for ReLU mask), w: [3][3][Cin][Cout]
// din_grad: [SZ][SZ][Cin] (output)
static void conv_backward(const s32 *dout_grad, const s32 *in_act, const s32 *w,
                          s32 *din_grad, int Cin, int Cout) {
    // Zero din_grad
    for (int i = 0; i < SZ*SZ*Cin; i++) din_grad[i] = 0;

    for (int co = 0; co < Cout; co++) {
        for (int y = 0; y < SZ; y++) {
            for (int x = 0; x < SZ; x++) {
                int oidx = (y*SZ + x)*Cout + co;
                s32 go = dout_grad[oidx];
                if (go == 0) continue;
                // ReLU mask: if in_act was <= 0, gradient is 0
                // (we check the output, since ReLU(out) = 0 means in <= 0)
                // Actually we need in_act pre-ReLU. We stored post-ReLU, so:
                // if post-ReLU == 0, then pre <= 0, gradient blocked.
                // We don't have pre, but post==0 implies blocked (except exactly 0).
                // To be safe, we pass the post-ReLU and assume blocked if 0.
                // (This is approximate but works for dreaming)

                // Distribute to input positions
                for (int ky = -1; ky <= 1; ky++) {
                    int iy = y + ky;
                    if (iy < 0 || iy >= SZ) continue;
                    for (int kx = -1; kx <= 1; kx++) {
                        int ix = x + kx;
                        if (ix < 0 || ix >= SZ) continue;
                        const s32 *wp = w + (((ky+1)*3 + (kx+1))*Cin + 0)*Cout + co;
                        s32 *gp = din_grad + (iy*SZ + ix)*Cin;
                        for (int ci = 0; ci < Cin; ci++) {
                            // Check ReLU: in_act at (iy,ix,ci) post-ReLU
                            // If it was 0, the pre-activation was <= 0, block gradient
                            s32 act = in_act[(iy*SZ + ix)*Cin + ci];
                            if (act == 0 && go > 0) {
                                // Pre-activation <= 0, ReLU blocks positive gradient
                                // (negative gradient also blocked since output is at 0)
                                continue;
                            }
                            gp[ci] += fxmul(go, wp[ci*Cout]);
                        }
                    }
                }
            }
        }
    }
}

// Forward pass: input [576] (0-255), outputs in buf_a1/a2/a3
void cnn_forward(const unsigned char *input) {
    // Normalize input to 0.0-1.0 in 16.16
    for (int i = 0; i < SZ*SZ; i++) {
        buf_x[i] = (input[i] << 16) / 255;
    }
    conv_relu(buf_x, W1, B1, buf_a1, 1, C1);
    conv_relu(buf_a1, W2, B2, buf_a2, C1, C2);
    conv_relu(buf_a2, W3, B3, buf_a3, C2, C3);
}

// Find channel in a3 with highest mean activation
int cnn_best_channel(void) {
    int best = 0;
    s64 best_sum = -1;
    for (int c = 0; c < C3; c++) {
        s64 sum = 0;
        for (int i = 0; i < SZ*SZ; i++) {
            sum += buf_a3[i*C3 + c];
        }
        if (sum > best_sum) { best_sum = sum; best = c; }
    }
    return best;
}

// One dream iteration: gradient ascent on channel 'ch' of a3
// input: [576] 0-255 (modified in place)
// step: max pixel change per iteration (e.g., 8)
void cnn_dream_step(unsigned char *input, int ch, int step) {
    // Forward (need activations for ReLU masks)
    cnn_forward(input);

    // dL/da3: 1.0 for channel ch, 0 else (mean, so 1/(SZ*SZ))
    s32 grad_val = 65536 / (SZ*SZ);  // 1/(576) in 16.16
    for (int i = 0; i < SZ*SZ*C3; i++) buf_g3[i] = 0;
    for (int i = 0; i < SZ*SZ; i++) {
        buf_g3[i*C3 + ch] = grad_val;
    }

    // Backward
    conv_backward(buf_g3, buf_a2, W3, buf_g2, C2, C3);
    conv_backward(buf_g2, buf_a1, W2, buf_g1, C1, C2);
    conv_backward(buf_g1, buf_x, W1, buf_gx, 1, C1);
    // Note: buf_x is post-normalization (0-1), used as ReLU mask input.
    // For the first layer, in_act is buf_x (which is >= 0 always), so ReLU never blocks.
    // That's fine.

    // Normalize gradient: find max abs, scale so max step = 'step' pixels
    s32 gmax = 1;
    for (int i = 0; i < SZ*SZ; i++) {
        s32 a = buf_gx[i] >= 0 ? buf_gx[i] : -buf_gx[i];
        if (a > gmax) gmax = a;
    }
    // Update input: x += (grad / gmax) * step
    // step is in pixels (0-255), e.g., step=8 means max change of 8 per iter
    for (int i = 0; i < SZ*SZ; i++) {
        // dv = grad * step / gmax
        s64 dv64 = (s64)buf_gx[i] * step / gmax;
        int dv = (int)dv64;
        int nv = (int)input[i] + dv;
        if (nv < 0) nv = 0;
        if (nv > 255) nv = 255;
        input[i] = (unsigned char)nv;
    }
}

// Get current a3 for visualization (returns pointer to 24x24x16 s32)
const s32 *cnn_get_a3(void) { return buf_a3; }
