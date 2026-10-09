// Overworld: towns, warps, Bob, the Dream Generator building, encounters
#include "game.h"
#include "dreamnet.h"

#define T TILE_SIZE

static const MapDef *M;
static int cur_map;
static int pxl, pyl;            // player position in pixels (top-left of tile)
static int tx, ty;              // player tile
static int move_dx, move_dy, move_left; // current step (move_left: pixels to go)
static int move_q8, move_sx, move_sy;   // progress of the step (Q8 pixels) and where it started
static u32 last_vb;                     // screen refreshes seen by the last update
static int face;                // 0 F(down) 1 B(up) 2 L 3 R
static bool in_zone;
static u32 last_step_frame;
static char dialogue[200];
static int dialogue_action;     // 0 none, 1 enter generator
static int orbit;
static int bob_talks;
static int steps_since_enc;

typedef struct { s16 x, y; s8 vx, vy; u8 life, kind; u16 col; } Part;
static Part parts[40];

static const Area *A;

static bool cell_enc(int x, int y) {
    if (x < 0 || y < 0 || x >= M->cols || y >= M->rows) return false;
    return M->enc[y * M->cols + x] != 0;
}
static int cell_tile(int x, int y) { return M->tiles[y * M->cols + x]; }
static bool cell_water(int x, int y) { return (tile_flags[cell_tile(x, y)] & TILEF_WATER) != 0; }
static bool gen_at(int x, int y) {
    if (A->gen_x < 0) return false;
    return x >= A->gen_x && x < A->gen_x + 3 && y >= A->gen_y && y < A->gen_y + 2;
}
static bool bob_here(void) { return cur_map == MAP_ROOM1; }
#define BOB_X 2
#define BOB_Y 2

static bool solid(int x, int y) {
    if (x < 0 || y < 0 || x >= M->cols || y >= M->rows) return true;
    if (gen_at(x, y)) return true;
    return M->solid[y * M->cols + x] != 0;
}

static void say(const char *t, int action) {
    strncpy(dialogue, t, sizeof(dialogue) - 1);
    dialogue[sizeof(dialogue) - 1] = 0;
    dialogue_action = action;
}

static void load_map(int m, int spawn) {
    cur_map = m;
    M = &maps[m];
    A = area_for_map(m);
    if (spawn < 0 || spawn >= M->nspawns) spawn = 0;
    tx = M->spawns[spawn].x;
    ty = M->spawns[spawn].y;
    pxl = tx * T;
    pyl = ty * T;
    move_left = 0;
    in_zone = cell_enc(tx, ty);
    G.map = (u8)m;
    for (int i = 0; i < 40; i++) parts[i].life = 0;
}

void world_enter(void) {
    if (G.px != 255 && G.map < NUM_MAPS) {
        load_map(G.map, 0);
        tx = G.px;
        ty = G.py;
        if (tx >= M->cols || ty >= M->rows || solid(tx, ty)) load_map(G.map, 0);
        pxl = tx * T;
        pyl = ty * T;
        face = G.face & 3;
        in_zone = cell_enc(tx, ty);
    } else {
        load_map(MAP_MAP, 0);
        face = 0;
    }
    bob_talks = G.bob_talks;
    dialogue[0] = 0;
}

void world_resume(void) {
    // back from an encounter / binder / generator
    in_zone = cell_enc(tx, ty);
}

static void remember_pos(void) {
    G.map = (u8)cur_map;
    G.px = (u8)tx;
    G.py = (u8)ty;
    G.face = (u8)face;
}

static void finish_step(void) {
    // warps
    for (int i = 0; i < M->nwarps; i++) {
        const Warp *w = &M->warps[i];
        if (w->x == tx && w->y == ty && w->to_map >= 0) {
            load_map(w->to_map, w->entry);
            remember_pos();
            save_dirty = true;
            return;
        }
    }
    if (!G.water_said && cell_water(tx, ty)) {
        G.water_said = 1;
        say("Am I dreaming? I'm walking on this water.", 0);
        return;
    }
    bool z = cell_enc(tx, ty);
    if (z) {
        bool go;
        if (A->random_enc) {
            steps_since_enc++;
            go = steps_since_enc >= 2 && rnd_range(A->random_enc) == 0;
        } else {
            go = !in_zone;
        }
        in_zone = true;
        if (go) {
            steps_since_enc = 0;
            remember_pos();
            encounter_start();
            return;
        }
    } else {
        in_zone = false;
    }
}

static void try_step(int dx, int dy) {
    if (dx < 0) face = 2;
    else if (dx > 0) face = 3;
    else if (dy < 0) face = 1;
    else face = 0;
    int nx = tx + dx, ny = ty + dy;
    if (bob_here() && nx == BOB_X && ny == BOB_Y) {
        bob_talks++;
        G.bob_talks = (u8)MIN(bob_talks, 200);
        say(bob_talks >= 2 ? "Be careful though, I get lost sometimes."
                           : "There's some crazy things in the water over there, go for a swim!", 0);
        return;
    }
    if (gen_at(nx, ny)) {
        say("A Dream Generator hums inside. It dreams trading cards you can zoom into forever. Step in?  A = yes, B = no", 1);
        return;
    }
    if (solid(nx, ny)) return;
    move_dx = dx;
    move_dy = dy;
    move_left = T;
    move_q8 = 0;
    move_sx = pxl;
    move_sy = pyl;
    tx = nx;
    ty = ny;
    last_step_frame = frame_no;
}

// ---------------------------------------------------------------- drawing
static void draw_generator(Surf *s, int camx, int camy) {
    if (A->gen_x < 0) return;
    int gx = A->gen_x * T - camx, gy = A->gen_y * T - camy, gw = 3 * T, gh = 2 * T;
    if (gx > SCREEN_W || gy - 20 > SCREEN_H || gx + gw < 0 || gy + gh < 0) return;
    int t = (int)frame_no;
    int hue = (t * 3) % 360, pulse = 128 + (isin(t * 7) >> 5); // 0..256
    // shadow
    for (int y = 0; y < 5; y++) s_rect_blend(s, gx - 2 + y, gy + gh - 3 + y / 2, gw + 4 - 2 * y, 1, COL(0, 0, 0), 90);
    int wl = gx + 2, wr = gx + gw - 2, wall_top = gy + 6, base = gy + gh - 1;
    for (int y = wall_top; y < base; y++) {
        int k = (y - wall_top) * 256 / MAX(1, base - wall_top);
        s_rect(s, wl, y, wr - wl, 1, blend(COL8(61, 38, 112), COL8(23, 13, 46), k));
    }
    for (int y = wall_top + 3, row = 0; y < base; y += 3, row++) {
        s_rect_blend(s, wl, y, wr - wl, 1, COL(0, 0, 0), 70);
        for (int x = wl + (row & 1 ? 2 : 5); x < wr; x += 6) s_rect_blend(s, x, y - 3, 1, 3, COL(0, 0, 0), 70);
    }
    s_box(s, wl, wall_top, wr - wl, base - wall_top, COL(1, 1, 3));
    for (int side = 0; side < 2; side++) {
        int wx = side ? wr - 9 : wl + 3, wy = wall_top + 7;
        s_rect(s, wx, wy, 6, 6, blend(hsv(hue + side * 120, 230, 180), COL(31, 31, 31), pulse / 3));
        s_box(s, wx, wy, 6, 6, COL(1, 1, 3));
        s_rect(s, wx + 3, wy, 1, 6, COL(1, 1, 3));
        s_rect(s, wx, wy + 3, 6, 1, COL(1, 1, 3));
    }
    // roof
    int rt = gy - 5;
    for (int y = rt; y <= wall_top + 1; y++) {
        int k = (y - rt) * 256 / MAX(1, wall_top + 1 - rt);
        int inset = (gw / 6) * (256 - k) / 256;
        s_rect(s, gx - 1 + inset, y, gw + 2 - 2 * inset, 1, COL8(20, 10, 36));
        s_px(s, gx - 1 + inset, y, COL8(201, 160, 255));
        s_px(s, gx + gw - inset, y, COL8(201, 160, 255));
    }
    s_rect(s, gx + gw / 6, rt, gw - gw / 3, 1, COL8(201, 160, 255));
    // antenna + orb
    int ax = gx + gw * 3 / 4;
    s_rect(s, ax, rt - 8, 1, 8, COL8(138, 122, 168));
    s_circle(s, ax, rt - 9, 2 + (pulse >> 7), hsv(hue, 200, 255), 200);
    s_px(s, ax, rt - 9, COL(31, 31, 31));
    // sign
    mini_text(s, "DREAM GEN", gx + gw / 2 - mini_w("DREAM GEN") / 2, rt + 3, blend(COL8(138, 255, 234), COL(31, 31, 31), pulse / 4));
    // the dreaming eye
    int ex = gx + gw / 2, ey = wall_top + 9;
    for (int y = -4; y <= 4; y++)
        for (int x = -7; x <= 7; x++) {
            if (x * x * 16 + y * y * 49 > 49 * 16) continue;
            int d2 = x * x + y * y;
            u16 c = COL8(244, 236, 255);
            if (d2 <= 16) {
                int ang = (x * 64 + y * 97 + t * 4) & 1023;
                c = d2 <= 2 ? COL(1, 0, 2) : hsv(hue + d2 * 20 + (isin(ang) >> 7), 255, 230);
            }
            s_px(s, ex + x, ey + y, c);
        }
    // door
    int dw = 8, dh = 10, dx0 = gx + gw / 2 - dw / 2, dy0 = base - dh;
    for (int y = 0; y < dh; y++) {
        int inset = y < 3 ? 3 - y : 0;
        s_rect(s, dx0 + inset, dy0 + y, dw - 2 * inset, 1, blend(hsv(hue, 200, 255), COL8(42, 24, 80), y * 256 / dh));
    }
}

static void draw_person(Surf *s, const u16 *spr, int x, int y) {
    // shadow then sprite
    for (int i = 0; i < 3; i++) s_rect_blend(s, x + 3 + i, y + 21 + i / 2, 10 - 2 * i, 1, COL(0, 0, 0), 80);
    s_blit(s, spr, HERO_W, HERO_H, x, y);
}

static void draw_companion(Surf *s, int pcx, int pcy, bool front) {
    if (!G.companion.kind) return;
    int a = orbit & 1023;
    int sn = isin(a);
    if ((sn >= 0) != front) return;
    int ox = pcx + (icos(a) * 14 >> 12), oy = pcy + (sn * 7 >> 12) - 4;
    s_rect_blend(s, ox - 5, oy + 9, 10, 2, COL(0, 0, 0), 80);
    draw_pick(s, &G.companion, ox, oy, 20);
}

static void update_particles(int camx, int camy, int c0, int r0, int c1, int r1) {
    // falling leaves from the big trees, rising dream motes
    if ((frame_no & 3) == 0) {
        int tries = 4;
        while (tries--) {
            int x = c0 + rnd_range(c1 - c0 + 1), y = r0 + rnd_range(r1 - r0 + 1);
            if (x < M->cols && y < M->rows && (tile_flags[cell_tile(x, y)] & TILEF_TREE)) {
                for (int i = 0; i < 40; i++)
                    if (!parts[i].life) {
                        static const u16 LC[6] = {COL8(106, 168, 76), COL8(201, 160, 76), COL8(76, 175, 154), COL8(143, 191, 90), COL8(217, 140, 255), COL8(122, 214, 192)};
                        parts[i] = (Part){(s16)(x * T + rnd_range(T)), (s16)(y * T + 4), 0, 1, 90, 0, LC[rnd_range(6)]};
                        break;
                    }
                break;
            }
        }
    }
    if (rnd_range(14) == 0)
        for (int i = 0; i < 40; i++)
            if (!parts[i].life) {
                static const u16 MC[3] = {COL8(138, 255, 234), COL8(201, 160, 255), COL8(255, 224, 138)};
                parts[i] = (Part){(s16)(camx + rnd_range(SCREEN_W)), (s16)(camy + SCREEN_H + 4), 0, -1, 200, 1, MC[rnd_range(3)]};
                break;
            }
    for (int i = 0; i < 40; i++) {
        Part *p = &parts[i];
        if (!p->life) continue;
        p->life--;
        if (p->kind == 0) {
            p->y += (frame_no & 1);
            p->x += isin((frame_no * 8 + i * 100) & 1023) > 0 ? ((frame_no & 3) == 0) : -((frame_no & 3) == 0);
        } else {
            if ((frame_no & 1) == 0) p->y += p->vy;
            p->x += isin((frame_no * 6 + i * 77) & 1023) > 2000 && (frame_no & 3) == 0 ? 1 : 0;
        }
    }
}

static void draw_particles(Surf *s, int camx, int camy) {
    for (int i = 0; i < 40; i++) {
        Part *p = &parts[i];
        if (!p->life) continue;
        int x = p->x - camx, y = p->y - camy;
        if (p->kind == 0) {
            s_px(s, x, y, p->col);
            s_px(s, x + 1, y, p->col);
            s_px(s, x + 1, y + 1, blend(p->col, COL(0, 0, 0), 60));
        } else {
            int a = 60 + (isin((frame_no * 10 + i * 50) & 1023) >> 6);
            s_circle(s, x, y, 2, p->col, MAX(20, a));
            s_px(s, x, y, blend(p->col, COL(31, 31, 31), 160));
        }
    }
}

static void draw_dialogue(Surf *s) {
    int bh = 44, by = SCREEN_H - bh - 4;
    s_panel(s, 4, by, SCREEN_W - 8, bh, COL(1, 1, 4), COL8(201, 160, 255));
    s_text_wrap(s, dialogue, 12, by + 6, SCREEN_W - 26, 10, COL(31, 31, 31), 3);
    if ((frame_no / 20) & 1) s_text(s, "\x05", SCREEN_W - 16, by + bh - 12, COL8(138, 255, 234), 1);
}

static int area_count(void) {
    int n = 0;
    for (int i = A->first; i <= A->last; i++) n += G.caught[i] ? 1 : 0;
    return n;
}

static void draw_world(void) {
    Surf *s = &S_TOP;
    int camx = pxl + T / 2 - SCREEN_W / 2, camy = pyl + T / 2 - SCREEN_H / 2 - 4;
    int mapw = M->cols * T, maph = M->rows * T;
    if (mapw > SCREEN_W) camx = CLAMP(camx, 0, mapw - SCREEN_W);
    else camx = (mapw - SCREEN_W) / 2;
    if (maph > SCREEN_H) camy = CLAMP(camy, 0, maph - SCREEN_H);
    else camy = (maph - SCREEN_H) / 2;
    s_fill(s, COL(2, 1, 4));
    int c0 = MAX(0, camx / T), r0 = MAX(0, camy / T);
    int c1 = MIN(M->cols - 1, (camx + SCREEN_W) / T), r1 = MIN(M->rows - 1, (camy + SCREEN_H) / T);
    int t = (int)frame_no;
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++) {
            int id = M->tiles[r * M->cols + c];
            int dx = c * T - camx, dy = r * T - camy;
            const u16 *tp = tile_pixels + id * T * T;
            for (int y = 0; y < T; y++) {
                int yy = dy + y;
                if (yy < 0 || yy >= SCREEN_H) continue;
                u16 *dst = s->px + yy * SCREEN_W;
                const u16 *src = tp + y * T;
                if (dx >= 0 && dx + T <= SCREEN_W) memcpy(dst + dx, src, T * 2);
                else
                    for (int x = 0; x < T; x++)
                        if ((unsigned)(dx + x) < SCREEN_W) dst[dx + x] = src[x];
            }
            if (tile_flags[id] & TILEF_WATER) {
                // shimmer: a soft highlight line drifting through the water
                int ph = (t * 6 + c * 150 + r * 100) & 1023;
                int wy = dy + 4 + ((isin(ph) + 4096) * 8 >> 13);
                s_rect_blend(s, dx, wy, T, 1, COL(31, 31, 31), 60);
                if (((t >> 3) + c * 7 + r * 13) % 23 == 0) s_px(s, dx + ((c * 5 + t) & 15), dy + ((r * 3) & 15), COL(31, 31, 31));
            }
            int ov = M->over[r * M->cols + c];
            if (ov != 255) s_blit(s, tile_pixels + ov * T * T, T, T, dx, dy);
            if (M->enc[r * M->cols + c] && !A->random_enc) {
                int a = 40 + ((isin((t * 12 + (c + r) * 60) & 1023) + 4096) >> 8);
                s_rect_blend(s, dx, dy, T, T, COL8(150, 90, 255), a);
            }
        }
    draw_generator(s, camx, camy);
    if (bob_here()) {
        int bx = BOB_X * T - camx, by = BOB_Y * T - camy + T - HERO_H + ((isin(t * 8) * 1) >> 12);
        draw_person(s, bob_pixels, bx, by);
    }
    // player
    int bounce = 0;
    u32 since = frame_no - last_step_frame;
    if (since < 8) bounce = -(isin((int)since * 64) * 2 >> 12);
    int pxs = pxl - camx, pys = pyl - camy + T - HERO_H + 1 + bounce;
    int pcx = pxs + T / 2, pcy = pyl - camy + T / 2;
    orbit += 12;
    draw_companion(s, pcx, pcy, false);
    static const int FACE_FRAME[4] = {0, 1, 2, 3};
    draw_person(s, hero_pixels + FACE_FRAME[face] * HERO_W * HERO_H, pxs, pys);
    draw_companion(s, pcx, pcy, true);
    floaters_draw(s, cur_map, camx, camy, mapw, maph, pcx, pcy);
    update_particles(camx, camy, c0, r0, c1, r1);
    draw_particles(s, camx, camy);
    // HUD
    char hud[64];
    char num[8];
    strcpy(hud, A->name);
    strcat(hud, "  ");
    int cnt = area_count(), tot = A->last - A->first + 1;
    num[0] = (char)('0' + cnt / 10); num[1] = (char)('0' + cnt % 10); num[2] = '/';
    num[3] = (char)('0' + tot / 10); num[4] = (char)('0' + tot % 10); num[5] = 0;
    strcat(hud, cnt < 10 ? num + 1 : num);
    int hw = text_w(hud, 1) + 12;
    s_rect_blend(s, 3, 3, hw, 13, COL(1, 1, 3), 190);
    s_text(s, hud, 9, 6, COL8(201, 160, 255), 1);
    if (dialogue[0]) draw_dialogue(s);
}

static void draw_menu(void) {
    Surf *s = &S_BOT;
    draw_bg_pattern(s, (int)frame_no, COL(3, 1, 7), COL(7, 3, 14));
    s_panel(s, 8, 8, SCREEN_W - 16, 70, COL(2, 1, 5), COL8(201, 160, 255));
    s_text_sh(s, "Deepdreamlings DS", 18, 14, COL8(201, 160, 255), 1);
    s_text(s, A->name, 18, 28, COL8(138, 255, 234), 1);
    char line[48];
    int total = 0;
    for (int i = 1; i <= NUM_DREAMLINGS; i++) total += G.caught[i] ? 1 : 0;
    strcpy(line, "Caught: ");
    char d[6];
    d[0] = (char)('0' + total / 10); d[1] = (char)('0' + total % 10); d[2] = 0;
    strcat(line, total < 10 ? d + 1 : d);
    strcat(line, "   Fusions: ");
    int f = G.nfus;
    char e[6];
    e[0] = (char)('0' + f / 100); e[1] = (char)('0' + (f / 10) % 10); e[2] = (char)('0' + f % 10); e[3] = 0;
    strcat(line, f < 10 ? e + 2 : (f < 100 ? e + 1 : e));
    s_text(s, line, 18, 42, COL(26, 24, 30), 1);
    s_text(s, G.companion.kind ? "Companion:" : "No companion yet", 18, 58, COL(20, 18, 26), 1);
    if (G.companion.kind) {
        char nm[32];
        pick_name(&G.companion, nm);
        s_text(s, nm, 78, 58, COL8(255, 224, 138), 1);
        draw_pick(s, &G.companion, SCREEN_W - 40, 44, 40);
    }
    if (ui_button(s, (Rect){12, 88, 112, 40}, "Binder", COL8(201, 160, 255), true) && !dialogue[0]) {
        remember_pos();
        binder_open(MODE_WORLD);
        return;
    }
    if (ui_button(s, (Rect){132, 88, 112, 40}, plat_fs_ok() ? "Save" : "No SD card", COL8(138, 255, 234), plat_fs_ok())) {
        remember_pos();
        save_write();
        say(plat_fs_ok() ? "Saved to the SD card." : "No SD card found, so the game can't save.", 0);
    }
    s_text(s, "D-pad walk   B (hold) run   A talk", 12, 140, COL(20, 18, 26), 1);
    int nd = floaters_in_town(cur_map);
    if (nd) {
        char fl[40] = "Dreams floating here: ";
        char c[4] = {(char)('0' + nd), 0, 0, 0};
        strcat(fl, c);
        s_text(s, fl, 12, 152, COL8(255, 224, 138), 1);
    } else s_text(s, "Walk into water to meet dreamlings.", 12, 152, COL(20, 18, 26), 1);
    s_text(s, "START binder   SELECT save", 12, 164, COL(20, 18, 26), 1);
    if (save_dirty && plat_fs_ok() && (frame_no % 1800) == 0) { remember_pos(); save_write(); }
}

void world_update(void) {
    u32 down = plat_keys_down(), held = plat_keys_held();
    if (dialogue[0]) {
        if (down & K_A) {
            int act = dialogue_action;
            dialogue[0] = 0;
            dialogue_action = 0;
            if (act == 1) {
                remember_pos();
                save_write();
                generator_open(A->model);
                return;
            }
        } else if ((down & K_B) && dialogue_action) {
            dialogue[0] = 0;
            dialogue_action = 0;
        }
    } else {
        if (move_left == 0) {
#ifdef PC_BUILD
            // test builds only: L+R+SELECT jumps to the next map
            if ((held & K_L) && (held & K_R) && (down & K_SELECT)) {
                load_map((cur_map + 1) % NUM_MAPS, 0);
                remember_pos();
                return;
            }
#endif
            if (held & K_LEFT) try_step(-1, 0);
            else if (held & K_RIGHT) try_step(1, 0);
            else if (held & K_UP) try_step(0, -1);
            else if (held & K_DOWN) try_step(0, 1);
            if (down & K_START) { remember_pos(); binder_open(MODE_WORLD); return; }
            if (down & K_SELECT) {
                remember_pos();
                say(save_write() ? "Saved to the SD card." : "No SD card found, so the game can't save.", 0);
            }
        }
        if (move_left > 0) {
            // speed is per screen refresh, so walking stays quick even when a
            // busy frame takes two refreshes to draw
            u32 vb = plat_vblanks();
            int ticks = CLAMP((int)(vb - last_vb), 1, 4);
            int sp_q8 = (held & K_B) ? 5 * 256 : 3 * 256;   // pixels per refresh: walk 3, run 5
            move_q8 = MIN(move_q8 + sp_q8 * ticks, T * 256);
            int done = move_q8 >> 8;
            pxl = move_sx + move_dx * done;
            pyl = move_sy + move_dy * done;
            move_left = T - done;
            if (move_left == 0) finish_step();
        }
    }
    last_vb = plat_vblanks();
    if (game_mode != MODE_WORLD) return;
    draw_world();
    // the touch screen only needs redrawing every other frame (or when touched)
    if (!(frame_no & 1) || touch_down || touch_held) draw_menu();
}

void world_say(const char *t) { say(t, 0); }
