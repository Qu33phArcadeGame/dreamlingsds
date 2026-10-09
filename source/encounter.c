// Encounter: the dream slot machine. Every spin fuses all three dreamlings.
#include "game.h"

enum { E_GROW, E_READY, E_SPIN, E_FUSE, E_RESULT, E_PROMPT };
static int phase, t_phase, spin_t;
static int nslot;   // 2 in Dream Core, 3 later on
static struct { u8 num, tier, show; bool stopped; int stop_at; } slot[3];
static Fusion fused;
static int fused_index;
static char msg[120];
static const Area *A;

#define FUSE_FRAMES 150
#define REEL_W 76
#define REEL_H 84
#define REEL_Y 28
static int reel_x(int i) {
    int total = nslot * REEL_W + (nslot - 1) * 8;   // reels centred on the screen
    return (SCREEN_W - total) / 2 + i * (REEL_W + 8);
}

static int roll_tier(void) {
    int r = rnd_range(100);   // 50% common, 35% rare, 15% holo (same as the website)
    return r < 50 ? TIER_RED : (r < 85 ? TIER_GREEN : TIER_BLUE);
}
static int rand_num(void) { return A->first + rnd_range(A->last - A->first + 1); }

void encounter_start(void) {
    A = area_for_map(G.map);
    nslot = CLAMP(A->slots, 2, 3);
    for (int i = 0; i < nslot; i++) {
        slot[i].num = (u8)rand_num();
        slot[i].tier = (u8)roll_tier();
        slot[i].show = (u8)rand_num();
        slot[i].stopped = false;
        slot[i].stop_at = (i + 1) * 38 + 18;
    }
    phase = E_GROW;
    t_phase = 0;
    msg[0] = 0;
    set_mode(MODE_ENCOUNTER);
}

static void resolve(void) {
    memset(&fused, 0, sizeof(fused));
    fused.n = (u8)nslot;
    bool all = true, any = false;
    for (int i = 0; i < nslot; i++) {
        fused.comp[i] = slot[i].num;
        fused.tier[i] = slot[i].tier;
        fused.power += slot[i].num;
        if (slot[i].tier != TIER_RED) any = true;
        else all = false;
    }
    fused.rare = all ? 2 : (any ? 1 : 0);
    add_fusion(&fused);
    fused_index = G.nfus - 1;
    char nm[32];
    fusion_name(&fused, nm);
    const char *kind = fused.rare == 2 ? "a full-rare " : (fused.rare == 1 ? "a rare " : "a ");
    strcpy(msg, "They fused into ");
    strcat(msg, kind);
    strcat(msg, nslot >= 3 ? "Tri-being: " : "Fusion: ");
    strcat(msg, nm);
    strcat(msg, "! Try it in the Dream Generator.");
    phase = E_FUSE;
    t_phase = 0;
}

static int ease_out(int t) { // t 0..256 -> 0..256
    int u = 256 - t;
    return 256 - ((u * u >> 8) * u >> 8);
}

static void draw_reveal(Surf *s, int p) {
    int cx = 128, cy = 72;
    int best = fusion_best_tier(&fused);
    u16 col = TIER_COLOR[best];
    int pop = p < 154 ? ease_out(p * 256 / 154) * 118 / 100 : 302 - ((p - 154) * 46 / 102);
    int size = 64 * pop / 256 * 3 / 2;
    // rays
    int rot = (int)(frame_no * 2);
    for (int k = 0; k < 12; k++) {
        int a = rot + k * 85;
        for (int r = 18; r < 110; r += 2) {
            int w = r / 18;
            for (int j = -w; j <= w; j++) {
                int aa = a + j * 6;
                int x = cx + (icos(aa) * r >> 12), y = cy + (isin(aa) * r >> 12);
                if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H) s->px[y * SCREEN_W + x] = blend(s->px[y * SCREEN_W + x], k & 1 ? col : COL(31, 31, 31), 50);
            }
        }
    }
    s_circle(s, cx, cy, size / 2 + 6, COL(31, 31, 31), 50);
    draw_fusion(s, &fused, cx, cy + (isin((int)frame_no * 10) * 2 >> 12), MAX(8, size));
    for (int k = 0; k < 10; k++) {
        int a = k * 102 + (int)frame_no * 5;
        int rr = size * 55 / 100 + (isin((int)frame_no * 12 + k * 90) * 6 >> 12);
        int x = cx + (icos(a) * rr >> 12), y = cy + (isin(a) * rr * 8 / 10 >> 12);
        if ((frame_no / 4 + k) & 1) s_rect(s, x, y, 2, 2, COL(31, 31, 31));
    }
}

static void draw_fuse_anim(Surf *s) {
    int f = t_phase * 256 / FUSE_FRAMES;  // 0..256
    int cx = 128, cy = 72;
    int best = fusion_best_tier(&fused);
    u16 col = TIER_COLOR[best];
    if (f < 200) {
        int lift = ease_out(MIN(256, f * 256 / 64));
        int spiral = MAX(0, (f - 51) * 256 / 149);
        int R = 70 * (256 - ease_out(MIN(256, spiral)) * 92 / 100) >> 8;
        int spin = (spiral * spiral >> 8) * 14 * 1024 / (256 * 6);
        s_circle(s, cx, cy, 70, col, 30 + spiral / 3);
        s_circle(s, cx, cy, 30, COL(31, 31, 31), spiral / 3);
        for (int i = 0; i < nslot; i++) {
            int a = spin + i * 1024 / nslot - 256;
            int rx = reel_x(i) + REEL_W / 2, ry = REEL_Y + REEL_H / 2;
            int tx = cx + (icos(a) * R >> 12), ty = cy + (isin(a) * R * 8 / 10 >> 12);
            int x = rx + (tx - rx) * lift / 256, y = ry + (ty - ry) * lift / 256;
            int sz = 64 * (256 - spiral * 35 / 100) >> 8;
            if (spiral > 0)
                for (int k = 1; k < 8; k++) {
                    int aa = a - k * 24;
                    s_circle(s, cx + (icos(aa) * R >> 12), cy + (isin(aa) * R * 8 / 10 >> 12), 3, TIER_COLOR[slot[i].tier], spiral / (k + 1));
                }
            draw_dreamling(s, slot[i].num, slot[i].tier, x, y, sz, 256);
        }
    }
    if (f >= 179 && f < 230) {
        int d = f - 200;
        int fl = 256 - IABS(d) * 256 / 26;
        if (fl > 0) {
            s_rect_blend(s, 0, 0, SCREEN_W, SCREEN_H, COL(31, 31, 31), MIN(256, fl));
            s_ring(s, cx, cy, 6 + (f - 179) * 3, col);
            s_ring(s, cx, cy, 7 + (f - 179) * 3, col);
        }
    }
    if (f >= 200) draw_reveal(s, MIN(256, (f - 200) * 256 / 56));
}

static void draw_top(void) {
    Surf *s = &S_TOP;
    int t = (int)frame_no;
    // dreamy swirl background in the area's colours
    u16 c1 = A->tint, c2 = blend(A->tint, COL(31, 20, 31), 90);
    for (int y = 0; y < SCREEN_H; y += 4)
        for (int x = 0; x < SCREEN_W; x += 4) {
            int dx = x - 128, dy = y - 96;
            int ang = (dx * 3 + dy * 2 + t * 4) & 1023;
            int v = isin(ang + ((dx * dx + dy * dy) >> 6)) >> 5; // -128..128
            u16 c = blend(c1, c2, 128 + v);
            for (int yy = 0; yy < 4; yy++) {
                u16 *p = s->px + (y + yy) * SCREEN_W + x;
                p[0] = p[1] = p[2] = p[3] = c;
            }
        }
    // the hero from behind, standing in the dream
    bool hide = (phase == E_FUSE && t_phase > FUSE_FRAMES * 6 / 10) || phase >= E_RESULT;
    if (!hide) {
        s_rect_blend(s, 112, 184, 32, 4, COL(0, 0, 0), 90);
        s_blit_ex(s, hero_pixels + 1 * HERO_W * HERO_H, HERO_W, HERO_H, 112, 140 + (isin(t * 6) * 2 >> 12), 32, 48, 0, 256, false);
    }
    if (phase == E_READY || phase == E_SPIN) {
        s_panel(s, reel_x(0) - 4, REEL_Y - 4, reel_x(nslot - 1) + REEL_W + 4 - (reel_x(0) - 4), REEL_H + 8, COL(3, 1, 7), COL8(201, 160, 255));
        for (int i = 0; i < nslot; i++) {
            int rx = reel_x(i);
            s_rect(s, rx, REEL_Y, REEL_W, REEL_H, COL(1, 0, 3));
            int num = phase == E_READY ? slot[i].num : slot[i].show;
            int tier = (slot[i].stopped || phase == E_READY) ? slot[i].tier : TIER_RED;
            int wob = (!slot[i].stopped && phase == E_SPIN) ? (isin(spin_t * 330) * 5 >> 12) : 0;
            draw_dreamling(s, num, tier, rx + REEL_W / 2, REEL_Y + REEL_H / 2 + wob, 64, 256);
            u16 bc = slot[i].stopped ? TIER_COLOR[slot[i].tier] : COL(11, 9, 15);
            s_box(s, rx, REEL_Y, REEL_W, REEL_H, bc);
            if (slot[i].stopped) s_box(s, rx + 1, REEL_Y + 1, REEL_W - 2, REEL_H - 2, bc);
        }
    }
    if (phase == E_FUSE) draw_fuse_anim(s);
    if (phase >= E_RESULT) draw_reveal(s, 256);
    const char *top = phase == E_READY ? (nslot == 2 ? "Press A to spin. Both will fuse!" : "Press A to spin. All three will fuse!")
                      : phase == E_SPIN ? "Spinning..."
                      : phase == E_FUSE ? "Fusing!"
                      : phase >= E_RESULT ? "Press A to continue" : "";
    if (top[0]) {
        s_rect_blend(s, 0, 4, SCREEN_W, 13, COL(1, 0, 3), 150);
        s_text_c(s, top, 128, 7, COL8(201, 160, 255), 1);
    }
    if (phase >= E_RESULT && msg[0]) {
        s_rect_blend(s, 0, 150, SCREEN_W, 40, COL(1, 0, 3), 170);
        s_text_wrap(s, msg, 8, 154, SCREEN_W - 16, 10, COL8(138, 255, 234), 3);
    }
    if (phase == E_GROW) {
        // the dream opening: a growing ring of light
        int r = t_phase * 12;
        s_ring(s, 128, 96, r, COL(31, 31, 31));
        s_ring(s, 128, 96, r + 2, A->tint);
    }
}

static void draw_bottom(bool *tap_a, bool *tap_b) {
    Surf *s = &S_BOT;
    s_fill(s, COL(2, 1, 5));
    s_panel(s, 6, 6, SCREEN_W - 12, 74, COL(3, 1, 7), COL8(201, 160, 255));
    s_text_sh(s, "Dream slots", 14, 12, COL8(201, 160, 255), 1);
    s_text(s, A->name, 14, 24, COL8(138, 255, 234), 1);
    for (int i = 0; i < nslot; i++) {
        int x = (SCREEN_W - nslot * 78) / 2 + 4 + i * 78;
        bool known = phase != E_SPIN || slot[i].stopped;
        if (phase == E_GROW) known = false;
        s_rect(s, x, 38, 70, 36, COL(1, 0, 3));
        if (known) {
            char nm[24];
            dreamling_name(slot[i].num, nm);
            draw_dreamling(s, slot[i].num, slot[i].tier, x + 14, 56, 28, 256);
            s_text(s, nm, x + 30, 42, COL(28, 27, 31), 1);
            static const char *const TN[3] = {"\x04 common", "\x02 rare", "\x03 holo"};
            s_text(s, TN[MIN(slot[i].tier, 2)], x + 30, 56, TIER_COLOR[slot[i].tier], 1);
        } else {
            s_text_c(s, "?", x + 35, 52, COL(12, 10, 16), 1);
        }
    }
    *tap_a = *tap_b = false;
    if (phase == E_READY) *tap_a = ui_button(s, (Rect){48, 96, 160, 54}, "SPIN!", COL8(255, 143, 191), true);
    else if (phase == E_RESULT) *tap_a = ui_button(s, (Rect){48, 96, 160, 54}, "Continue", COL8(138, 255, 234), true);
    else if (phase == E_PROMPT) {
        char nm[32];
        fusion_name(&fused, nm);
        s_text_c(s, "Set as your companion?", 128, 90, COL(30, 30, 31), 1);
        s_text_c(s, nm, 128, 102, COL8(255, 224, 138), 1);
        *tap_a = ui_button(s, (Rect){20, 120, 100, 44}, "Yes (A)", COL8(63, 224, 176), true);
        *tap_b = ui_button(s, (Rect){136, 120, 100, 44}, "No (B)", COL8(255, 143, 191), true);
    } else {
        s_text_c(s, phase == E_FUSE ? "Fusing..." : "...", 128, 116, COL(20, 18, 26), 1);
    }
    s_text(s, "Rarity per slot: 50% common, 35% rare, 15% holo", 8, 178, COL(13, 11, 18), 1);
}

static void finish(bool set_companion) {
    if (set_companion) {
        G.companion.kind = 2;
        G.companion.fus = (u16)fused_index;
    }
    save_write();
    set_mode(MODE_WORLD);
    world_resume();
    if (!G.tut_done) {
        G.tut_done = 1;
        world_say("Nice! Fusions can circle you as a companion. Hold B to run faster.");
    }
}

void encounter_update(void) {
    u32 down = plat_keys_down();
    t_phase++;
    switch (phase) {
    case E_GROW:
        if (t_phase > 22) { phase = E_READY; t_phase = 0; }
        break;
    case E_SPIN:
        spin_t++;
        {
            bool all = true;
            for (int i = 0; i < nslot; i++) {
                if (slot[i].stopped) continue;
                if (spin_t % 3 == 0) slot[i].show = (u8)rand_num();
                if (spin_t >= slot[i].stop_at) { slot[i].stopped = true; slot[i].show = slot[i].num; }
                else all = false;
            }
            if (all && spin_t > slot[nslot - 1].stop_at + 20) resolve();
        }
        break;
    case E_FUSE:
        if (t_phase >= FUSE_FRAMES) { phase = E_RESULT; t_phase = 0; }
        break;
    }
    draw_top();
    bool tap_a, tap_b;
    draw_bottom(&tap_a, &tap_b);
    if (phase == E_READY && ((down & K_A) || tap_a)) { phase = E_SPIN; spin_t = 0; t_phase = 0; }
    else if (phase == E_RESULT && ((down & K_A) || tap_a) && t_phase > 10) { phase = E_PROMPT; t_phase = 0; }
    else if (phase == E_PROMPT && t_phase > 5) {
        if ((down & K_A) || tap_a) finish(true);
        else if ((down & K_B) || tap_b) finish(false);
    }
}
