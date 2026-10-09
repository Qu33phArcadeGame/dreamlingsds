// Deepdreamlings DS - main loop and title screen
#include "game.h"
#include "dreamnet.h"

int game_mode = MODE_TITLE;
u32 frame_no;
static bool has_save;
static int title_t;

void set_mode(int m) {
    game_mode = m;
    // clear both screens so nothing from the old mode lingers
    s_fill(&S_TOP, COL(1, 0, 3));
    s_fill(&S_BOT, COL(1, 0, 3));
}

static void title_update(void) {
    Surf *s = &S_TOP;
    title_t++;
    draw_bg_pattern(s, title_t * 2, COL(3, 0, 8), COL(10, 3, 18));
    // parade of dreamlings orbiting the title
    for (int i = 0; i < 11; i++) {
        int a = title_t * 3 + i * 93;
        int x = 128 + (icos(a) * 112 >> 12), y = 92 + (isin(a) * 74 >> 12);
        draw_dreamling(s, i + 1, (i % 4 == 3) ? TIER_BLUE : (i % 3 == 2 ? TIER_GREEN : TIER_RED), x, y, 32, 256);
    }
    int bob = isin(title_t * 8) * 2 >> 12;
    s_blit_pal(s, logo_idx, logo_pal, LOGO_W, LOGO_H, 128 - LOGO_W / 2, 58 + bob, LOGO_W, LOGO_H, 0, 256);
    s_text_sh(s, "DS", 128 - text_w("DS", 2) / 2, 116 + bob, COL8(138, 255, 234), 2);
    int tw = text_w("real neural dreaming on your DS", 1);
    s_rect_blend(s, 128 - tw / 2 - 4, 136, tw + 8, 12, COL(2, 0, 5), 180);
    s_text_c(s, "real neural dreaming on your DS", 128, 138, COL(26, 24, 30), 1);

    Surf *b = &S_BOT;
    draw_bg_pattern(b, title_t, COL(2, 0, 5), COL(6, 2, 12));
    bool go_continue = false, go_new = false;
    if (has_save) {
        go_continue = ui_button(b, (Rect){40, 40, 176, 44}, "Continue", COL8(138, 255, 234), true);
        go_new = ui_button(b, (Rect){40, 96, 176, 34}, "New game", COL(9, 7, 14), true);
    } else {
        go_new = ui_button(b, (Rect){40, 56, 176, 50}, "Start dreaming", COL8(138, 255, 234), true);
    }
    const char *sd = plat_fs_ok() ? "SD card found: your game saves." : "No SD card: your game won't be saved.";
    s_text_c(b, sd, 128, 150, plat_fs_ok() ? COL(20, 26, 22) : COL(28, 18, 20), 1);
    char cpu[48] = "Running on ";
    strcat(cpu, plat_cpu_label());
    mini_text(b, cpu, 128 - mini_w(cpu) / 2, 168, COL(14, 12, 20));
    u32 down = plat_keys_down();
    if ((down & (K_START | K_A)) && !go_new) go_continue = has_save, go_new = !has_save;
    if (go_new) {
        save_defaults();
#ifdef PC_BUILD
        // test builds: DL_FUSIONS=n starts with n random fusions in the binder
        extern char *getenv(const char *);
        extern int atoi(const char *);
        const char *ev = getenv("DL_FUSIONS");
        for (int i = 0, n = ev ? atoi(ev) : 0; i < n; i++) {
            Fusion f;
            memset(&f, 0, sizeof(f));
            f.n = 3;
            for (int k = 0; k < 3; k++) { f.comp[k] = (u8)(1 + rnd_range(11)); f.tier[k] = (u8)rnd_range(3); f.power += f.comp[k]; }
            f.rare = 1;
            add_fusion(&f);
        }
#endif
        save_write();
        world_enter();
        set_mode(MODE_WORLD);
    } else if (go_continue) {
        world_enter();
        set_mode(MODE_WORLD);
    }
}

#ifdef PC_BUILD
void pc_load_script(const char *path, const char *out);
#endif

int main(int argc, char **argv) {
#ifdef PC_BUILD
    if (argc > 1) pc_load_script(argv[1], argc > 2 ? argv[2] : ".");
#else
    (void)argc;
    (void)argv;
#endif
    plat_init();
    rnd_seed(plat_ms() * 2654435761u + 12345);
    save_defaults();
    has_save = save_load();
    set_mode(MODE_TITLE);
    u32 last_sec = plat_ms();
    while (plat_running()) {
        plat_poll();
        ui_begin_frame();
        frame_no++;
        switch (game_mode) {
        case MODE_TITLE: title_update(); break;
        case MODE_WORLD: world_update(); break;
        case MODE_ENCOUNTER: encounter_update(); break;
        case MODE_BINDER: binder_update(); break;
        case MODE_GENERATOR: generator_update(); break;
        }
        u32 now = plat_ms();
        if (now - last_sec >= 1000) { G.play_seconds++; last_sec += 1000; }
        // while the generator is dreaming, don't wait for the screen refresh
        plat_present(game_mode != MODE_GENERATOR);
    }
    return 0;
}
