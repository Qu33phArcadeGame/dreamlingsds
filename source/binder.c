// Binder: every dreamling and fusion as a trading card. Set your companion,
// or fuse cards together (the website binder's fuse: parts are used up).
#include "game.h"
#include "card.h"

static int ret_mode;
static int tab;          // 0 dreamlings, 1 fusions
static int sel, scroll;
static int variant_tier; // which owned tier of a dreamling card is shown
static bool fuse_mode;
static u8 fuse_sel[4];
static int nfuse_sel;
static int anim_t;       // fuse animation frame (0 = none)
static Fusion anim_parts[4];
static int anim_n;
static Fusion anim_result;
static char note[80];
#define card_buf card_shared
static u16 art_buf[80 * 100];
static bool art_dirty = true;

#define COLS 5
#define CELL 46
#define GRID_X 13
#define GRID_Y 30
#define ROWS 2
#define ANIM_FRAMES 120

static int entries(void) { return tab == 0 ? NUM_DREAMLINGS : G.nfus; }
static int fus_at(int i) { return G.nfus - 1 - i; } // newest first

static int best_tier_of(int num) {
    for (int t = 3; t >= 0; t--)
        if (G.tiers[num] & (1 << t)) return t;
    return 0;
}
static int next_tier_of(int num, int cur) {
    for (int k = 1; k <= 4; k++) {
        int t = (cur + k) & 3;
        if (G.tiers[num] & (1 << t)) return t;
    }
    return cur;
}

static void current_pick(Pick *p) {
    memset(p, 0, sizeof(*p));
    if (tab == 0) {
        int num = sel + 1;
        if (num > NUM_DREAMLINGS || !G.caught[num]) return;
        p->kind = 1;
        p->num = (u8)num;
        p->tier = (u8)variant_tier;
    } else if (G.nfus && sel < G.nfus) {
        p->kind = 2;
        p->fus = (u16)fus_at(sel);
    }
}

static void select_entry(int i) {
    int n = entries();
    if (n <= 0) { sel = 0; scroll = 0; art_dirty = true; return; }
    sel = CLAMP(i, 0, n - 1);
    if (tab == 0) variant_tier = best_tier_of(sel + 1);
    if (sel / COLS < scroll) scroll = sel / COLS;
    if (sel / COLS >= scroll + ROWS) scroll = sel / COLS - ROWS + 1;
    art_dirty = true;
}

void binder_open(int return_mode) {
    ret_mode = return_mode;
    tab = G.nfus ? 1 : 0;
    fuse_mode = false;
    nfuse_sel = 0;
    anim_t = 0;
    note[0] = 0;
    select_entry(0);
    set_mode(MODE_BINDER);
}

static int fuse_find(int fi) {
    for (int i = 0; i < nfuse_sel; i++)
        if (fuse_sel[i] == fi) return i;
    return -1;
}

static void do_fuse(void) {
    if (nfuse_sel < 2) return;
    Fusion f;
    memset(&f, 0, sizeof(f));
    bool all = true, any = false;
    for (int i = 0; i < nfuse_sel; i++) {
        const Fusion *src = &G.fus[fuse_sel[i]];
        anim_parts[i] = *src;
        for (int k = 0; k < src->n && f.n < MAX_COMP; k++) {
            f.comp[f.n] = src->comp[k];
            f.tier[f.n] = src->tier[k];
            f.power += src->comp[k];
            if (src->tier[k]) any = true;
            else all = false;
            f.n++;
        }
    }
    f.rare = all ? 2 : (any ? 1 : 0);
    anim_n = nfuse_sel;
    anim_result = f;
    // remove the parts, highest index first, keeping the companion pointing right
    for (int a = 0; a < nfuse_sel; a++)
        for (int b = a + 1; b < nfuse_sel; b++)
            if (fuse_sel[b] > fuse_sel[a]) { u8 t = fuse_sel[a]; fuse_sel[a] = fuse_sel[b]; fuse_sel[b] = t; }
    for (int i = 0; i < nfuse_sel; i++) {
        int idx = fuse_sel[i];
        if (G.companion.kind == 2) {
            if (G.companion.fus == idx) G.companion.kind = 0;
            else if (G.companion.fus > idx) G.companion.fus--;
        }
        memmove(&G.fus[idx], &G.fus[idx + 1], (G.nfus - idx - 1) * sizeof(Fusion));
        G.nfus--;
    }
    add_fusion(&f);
    nfuse_sel = 0;
    fuse_mode = false;
    anim_t = 1;
    tab = 1;
    select_entry(0);
    char nm[32];
    fusion_name(&f, nm);
    strcpy(note, "New ");
    strcat(note, fusion_label(&f));
    strcat(note, ": ");
    strcat(note, nm);
    save_write();
}

// ---------------------------------------------------------------- drawing
static void draw_top(void) {
    Surf *s = &S_TOP;
    draw_bg_pattern(s, (int)frame_no / 2, COL(2, 1, 5), COL(6, 2, 11));
    if (anim_t) {
        // fuse animation: the parts spiral together, flash, the new card appears
        int f = anim_t * 256 / ANIM_FRAMES, cx = 128, cy = 92;
        if (f < 180) {
            int R = 80 * (256 - f * 256 / 180) >> 8, spin = f * f / 40;
            for (int i = 0; i < anim_n; i++) {
                int a = spin + i * 1024 / anim_n;
                draw_fusion(s, &anim_parts[i], cx + (icos(a) * R >> 12), cy + (isin(a) * R * 7 / 10 >> 12), 48);
            }
            s_circle(s, cx, cy, 20 + f / 8, TIER_COLOR[fusion_best_tier(&anim_result)], f / 3);
        } else {
            int fl = 256 - (f - 180) * 4;
            draw_fusion(s, &anim_result, cx, cy, 64 + (f - 180) / 3);
            if (fl > 0) s_rect_blend(s, 0, 0, SCREEN_W, SCREEN_H, COL(31, 31, 31), fl);
        }
        return;
    }
    Pick p;
    current_pick(&p);
    if (!p.kind) {
        s_panel(s, 4, 2, 135, 189, COL(2, 1, 5), COL(10, 8, 16));
        s_text_c(s, tab == 0 ? "Not caught yet" : "No fusions yet", 71, 80, COL(16, 14, 22), 1);
        s_text_c(s, "Spin the dream slots", 71, 96, COL(16, 14, 22), 1);
        s_text_c(s, "in the water!", 71, 108, COL(16, 14, 22), 1);
    } else {
        if (art_dirty) { make_card_art(&p, art_buf, 80, 100); art_dirty = false; }
        CardInfo ci;
        memset(&ci, 0, sizeof(ci));
        pick_name(&p, ci.name);
        pick_detail(&p, ci.detail);
        strcpy(ci.style, area_for_map(G.map)->name);
        ci.rarity = (u8)pick_rarity(&p);
        card_render(card_buf, &ci, art_buf, 80, 100, (int)frame_no);
        card_draw_small(s, card_buf, 4, 2);
    }
    // details on the right
    int x = 146;
    s_text_sh(s, tab == 0 ? "Dreamlings" : "Fusions", x, 6, COL8(201, 160, 255), 1);
    if (p.kind == 1) {
        char nm[32];
        dreamling_name(p.num, nm);
        s_text(s, nm, x, 24, COL(31, 31, 31), 1);
        char d[24];
        pick_detail(&p, d);
        mini_text(s, d, x, 37, COL8(138, 255, 234));
        u8 owned[4];
        int n = 0;
        for (int t = 0; t < 4; t++)
            if (G.tiers[p.num] & (1 << t)) owned[n++] = (u8)t;
        mini_text(s, "OWNED TIERS", x, 50, COL(18, 16, 24));
        draw_tier_dots(s, owned, n, x + 4 + (n - 1) * 4, 62);
        if (n > 1) mini_text(s, "L/R: SWITCH TIER", x, 72, COL(18, 16, 24));
    } else if (p.kind == 2) {
        const Fusion *f = &G.fus[p.fus];
        char nm[32];
        fusion_name(f, nm);
        s_text(s, nm, x, 24, COL(31, 31, 31), 1);
        mini_text(s, fusion_label(f), x, 37, COL8(255, 143, 191));
        char pw[16] = "POWER ";
        int v = f->power, k = 6;
        char d[6];
        int nd = 0;
        do { d[nd++] = (char)('0' + v % 10); v /= 10; } while (v);
        while (nd) pw[k++] = d[--nd];
        pw[k] = 0;
        mini_text(s, pw, x, 46, COL8(255, 224, 138));
        draw_tier_dots(s, f->tier, f->n, x + 4 + (f->n - 1) * 4, 60);
        for (int i = 0; i < f->n && i < 6; i++) {
            char cn[24];
            dreamling_name(f->comp[i], cn);
            mini_text(s, cn, x, 70 + i * 8, TIER_COLOR[f->tier[i]]);
        }
    }
    if (p.kind && G.companion.kind == p.kind && (p.kind == 1 ? G.companion.num == p.num : G.companion.fus == p.fus))
        s_text(s, "\x01 Companion", x, 124, COL8(255, 224, 138), 1);
    if (note[0]) s_text_wrap(s, note, x, 140, SCREEN_W - x - 4, 10, COL8(138, 255, 234), 4);
}

static void draw_bottom(void) {
    Surf *s = &S_BOT;
    s_fill(s, COL(2, 1, 5));
    if (ui_button(s, (Rect){4, 3, 82, 22}, "Dreamlings", tab == 0 ? COL8(201, 160, 255) : COL(7, 5, 12), true) && tab != 0) {
        tab = 0; fuse_mode = false; nfuse_sel = 0; select_entry(0);
    }
    if (ui_button(s, (Rect){90, 3, 82, 22}, "Fusions", tab == 1 ? COL8(201, 160, 255) : COL(7, 5, 12), true) && tab != 1) {
        tab = 1; select_entry(0);
    }
    if (ui_button(s, (Rect){176, 3, 76, 22}, "Back", COL8(255, 143, 191), true)) {
        save_write();
        set_mode(ret_mode);
        if (ret_mode == MODE_WORLD) world_resume();
        return;
    }
    int n = entries();
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            int i = (scroll + r) * COLS + c;
            int x = GRID_X + c * CELL, y = GRID_Y + r * CELL;
            Rect rc = {(s16)x, (s16)y, CELL - 4, CELL - 4};
            if (i >= n) { s_box(s, x, y, CELL - 4, CELL - 4, COL(4, 3, 7)); continue; }
            bool is_sel = i == sel;
            int fi = tab == 1 ? fus_at(i) : -1;
            bool fsel = tab == 1 && fuse_mode && fuse_find(fi) >= 0;
            s_rect(s, x, y, CELL - 4, CELL - 4, fsel ? COL(10, 4, 14) : COL(4, 2, 8));
            if (tab == 0) {
                int num = i + 1;
                if (G.caught[num]) draw_dreamling(s, num, best_tier_of(num), x + 21, y + 19, 32, 256);
                else s_text_c(s, "?", x + 21, y + 16, COL(10, 8, 14), 1);
                char d[4] = {(char)('0' + num / 10), (char)('0' + num % 10), 0};
                mini_text(s, d, x + 2, y + 34, COL(14, 12, 18));
            } else {
                draw_fusion(s, &G.fus[fi], x + 21, y + 20, 32);
                const Fusion *f = &G.fus[fi];
                if (f->rare) s_circle(s, x + 36, y + 5, 2, f->rare == 2 ? COL8(255, 210, 74) : COL8(226, 230, 236), 256);
                if (fsel) {
                    char k[2] = {(char)('1' + fuse_find(fi)), 0};
                    s_circle(s, x + 6, y + 6, 5, COL8(255, 143, 191), 256);
                    s_text(s, k, x + 4, y + 2, COL(2, 0, 4), 1);
                }
            }
            u16 bc = is_sel ? COL8(138, 255, 234) : (fsel ? COL8(255, 143, 191) : COL(9, 7, 14));
            s_box(s, x, y, CELL - 4, CELL - 4, bc);
            if (is_sel) s_box(s, x - 1, y - 1, CELL - 2, CELL - 2, bc);
            if (ui_tapped(rc)) {
                if (tab == 1 && fuse_mode) {
                    int k = fuse_find(fi);
                    if (k >= 0) { memmove(&fuse_sel[k], &fuse_sel[k + 1], nfuse_sel - k - 1); nfuse_sel--; }
                    else if (nfuse_sel < 4) fuse_sel[nfuse_sel++] = (u8)fi;
                }
                select_entry(i);
            }
        }
    // scroll arrows
    int rows_total = (n + COLS - 1) / COLS;
    if (scroll > 0 && ui_button(s, (Rect){238, 30, 16, 40}, "\x06", COL(8, 6, 14), true)) { scroll--; }
    if (scroll + ROWS < rows_total && ui_button(s, (Rect){238, 76, 16, 40}, "\x05", COL(8, 6, 14), true)) { scroll++; }
    Pick p;
    current_pick(&p);
    int by = 128;
    if (tab == 1 && fuse_mode) {
        char t[48] = "Pick 2-4 fusions to fuse (";
        char d[3] = {(char)('0' + nfuse_sel), ')', 0};
        strcat(t, d);
        s_text(s, t, 8, by - 4 + 2, COL(26, 24, 30), 1);
        if (ui_button(s, (Rect){8, by + 10, 116, 44}, "FUSE!", COL8(255, 143, 191), nfuse_sel >= 2)) do_fuse();
        if (ui_button(s, (Rect){132, by + 10, 116, 44}, "Cancel", COL(8, 6, 14), true)) { fuse_mode = false; nfuse_sel = 0; }
    } else {
        if (ui_button(s, (Rect){8, by, 116, 30}, "Set companion", COL8(255, 224, 138), p.kind != 0)) {
            G.companion = p;
            save_dirty = true;
            strcpy(note, "Companion set!");
        }
        if (ui_button(s, (Rect){132, by, 116, 30}, "Fuse cards", COL8(201, 160, 255), tab == 1 && G.nfus >= 2)) {
            fuse_mode = true;
            nfuse_sel = 0;
            strcpy(note, "Tap the cards to fuse, then FUSE!");
        }
        mini_text(s, "D-PAD MOVE  A COMPANION  X FUSE  L/R TIER  B BACK", 8, by + 40, COL(14, 12, 20));
    }
}

void binder_update(void) {
    u32 down = plat_keys_down();
    if (anim_t) {
        anim_t++;
        if (anim_t > ANIM_FRAMES) anim_t = 0;
        draw_top();
        draw_bottom();
        return;
    }
    if (down & K_LEFT) select_entry(sel - 1);
    if (down & K_RIGHT) select_entry(sel + 1);
    if (down & K_UP) select_entry(sel - COLS);
    if (down & K_DOWN) select_entry(sel + COLS);
    if ((down & (K_L | K_R)) && tab == 0) { variant_tier = next_tier_of(sel + 1, variant_tier); art_dirty = true; }
    if (down & K_Y) { tab ^= 1; fuse_mode = false; nfuse_sel = 0; select_entry(0); }
    if (down & K_A) {
        Pick p;
        current_pick(&p);
        if (tab == 1 && fuse_mode && p.kind) {
            int fi = fus_at(sel), k = fuse_find(fi);
            if (k >= 0) { memmove(&fuse_sel[k], &fuse_sel[k + 1], nfuse_sel - k - 1); nfuse_sel--; }
            else if (nfuse_sel < 4) fuse_sel[nfuse_sel++] = (u8)fi;
        } else if (p.kind) {
            G.companion = p;
            save_dirty = true;
            strcpy(note, "Companion set!");
        }
    }
    if (down & K_X) {
        if (fuse_mode && nfuse_sel >= 2) do_fuse();
        else if (tab == 1 && G.nfus >= 2) { fuse_mode = !fuse_mode; nfuse_sel = 0; }
    }
    if ((down & K_B) || (down & K_START)) {
        if (fuse_mode) { fuse_mode = false; nfuse_sel = 0; }
        else {
            save_write();
            set_mode(ret_mode);
            if (ret_mode == MODE_WORLD) world_resume();
            return;
        }
    }
    draw_top();
    if (game_mode == MODE_BINDER) draw_bottom();
}
