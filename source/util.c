#include "common.h"

// ---------------------------------------------------------------- misc helpers
static u32 rng_state = 0x1234567;
void rnd_seed(u32 s) { rng_state = s ? s : 1; }
u32 rnd(void) {
    u32 x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return rng_state = x;
}
int rnd_range(int n) { return n <= 1 ? 0 : (int)((rnd() >> 8) % (u32)n); }

static s16 sin_tab[257];
static bool sin_ready;
static void sin_init(void) {
    // quarter wave via a small polynomial (no floating point needed on the DS)
    for (int i = 0; i <= 256; i++) {
        // x in 0..1 (quarter turn); sin(pi/2 x) ~ x*(1.5708 - 0.6459x^2 + 0.0797x^4)
        s64 x = (s64)i * 4096 / 256;               // Q12
        s64 x2 = x * x >> 12, x4 = x2 * x2 >> 12;
        s64 v = x * (6434 - (2646 * x2 >> 12) + (326 * x4 >> 12)) >> 12;
        sin_tab[i] = (s16)CLAMP(v, 0, 4096);
    }
    sin_ready = true;
}
int isin(int a) {
    if (!sin_ready) sin_init();
    a &= 1023;
    if (a < 256) return sin_tab[a];
    if (a < 512) return sin_tab[512 - a];
    if (a < 768) return -sin_tab[a - 512];
    return -sin_tab[1024 - a];
}
int icos(int a) { return isin(a + 256); }

