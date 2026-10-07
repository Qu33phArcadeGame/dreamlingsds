// Deepdreamlings DS - BlocksDS version
// Real neural network dreaming on Nintendo DS
#include <nds.h>
#include <stdio.h>
#include <string.h>

// CNN (same as before, pure C)
#include "cnn.h"
#include "dreamlings.h"

// Framebuffers: draw to RAM buffer, DMA copy to VRAM (like qu33ph)
static u16 drawBuf[256*192] __attribute__((aligned(32)));
static u16 *vramTop;  // points to drawBuf for drawing
static u16 *vramReal; // actual VRAM from bgGetGfxPtr


#define RGB(r,g,b) ((r)|((g)<<5)|((b)<<10))

// ---- 3x5 font ----
static const u8 font[128][5] = {
    [' ']={0,0,0,0,0}, ['!']={2,2,2,0,2}, ['.']={0,0,0,0,2},
    [':']={0,2,0,2,0}, ['-']={0,0,7,0,0}, ['>']={4,2,1,2,4},
    ['0']={3,5,5,5,6}, ['1']={2,6,2,2,7}, ['2']={7,1,7,4,7},
    ['3']={7,1,7,1,7}, ['4']={5,5,7,1,1}, ['5']={7,4,7,1,7},
    ['6']={7,4,7,5,7}, ['7']={7,1,2,2,2}, ['8']={7,5,7,5,7},
    ['9']={7,5,7,1,7},
    ['A']={2,5,7,5,5}, ['B']={6,5,6,5,6}, ['C']={3,4,4,4,3},
    ['D']={6,5,5,5,6}, ['E']={7,4,6,4,7}, ['F']={7,4,6,4,4},
    ['G']={3,4,5,5,3}, ['H']={5,5,7,5,5}, ['I']={7,2,2,2,7},
    ['J']={1,1,1,5,2}, ['K']={5,5,6,5,5}, ['L']={4,4,4,4,7},
    ['M']={5,7,7,5,5}, ['N']={6,5,5,5,5}, ['O']={2,5,5,5,2},
    ['P']={6,5,6,4,4}, ['Q']={2,5,5,6,3}, ['R']={6,5,6,5,5},
    ['S']={3,4,2,1,6}, ['T']={7,2,2,2,2}, ['U']={5,5,5,5,7},
    ['V']={5,5,5,5,2}, ['W']={5,5,7,7,5}, ['X']={5,5,2,5,5},
    ['Y']={5,5,2,2,2}, ['Z']={7,1,2,4,7},
};

// ---- graphics (256x256 bitmap, but we use 256x192) ----
static void px(int x, int y, u16 c) {
    if (x >= 0 && x < 256 && y >= 0 && y < 192) vramTop[y*256+x] = c;
}
static void rect(int x, int y, int w, int h, u16 c) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            px(x+i, y+j, c);
}
static void clear(u16 c) {
    for (int i = 0; i < 256*192; i++) vramTop[i] = c;
}
static void text(const char *s, int x, int y, u16 color) {
    int cx = x;
    while (*s) {
        u8 ch = *s++;
        if (ch < 128) {
            for (int r = 0; r < 5; r++) {
                u8 bits = font[ch][r];
                for (int col = 0; col < 3; col++) {
                    if (bits & (1 << (2-col))) px(cx+col, y+r, color);
                }
            }
        }
        cx += 4;
    }
}
static void draw_sprite_scaled(const u8 *spr, int dx, int dy, int scale, u16 tint) {
    for (int y = 0; y < 24; y++) {
        for (int x = 0; x < 24; x++) {
            u8 v = spr[y*24+x];
            if (v == 0) continue;
            u16 c;
            if (tint) {
                int tr = (tint & 31), tg = ((tint>>5)&31), tb = ((tint>>10)&31);
                int g = v * 31 / 255;
                c = RGB((tr*g)/31, (tg*g)/31, (tb*g)/31);
            } else {
                int g = v * 31 / 255;
                c = RGB(g,g,g);
            }
            for (int sy = 0; sy < scale; sy++)
                for (int sx = 0; sx < scale; sx++)
                    px(dx + x*scale + sx, dy + y*scale + sy, c);
        }
    }
}

// ---- game state ----
enum { ST_TITLE, ST_SELECT, ST_WORLD, ST_DREAM, ST_SLOT, ST_GALLERY };
static int state = ST_TITLE;
static int selected = 0;
static int collected[11];
static int ncollected = 0;
static u8 dream_img[576];
static u8 fused[11][576];
static int nfused = 0;
static int px_x = 120, py_y = 90;
static const int gates[3][5] = {
    {30, 40, 36, 36, 0},
    {190, 40, 36, 36, 1},
    {110, 130, 36, 36, 2},
};
static const u16 gate_colors[3] = { RGB(31,0,0), RGB(0,31,0), RGB(0,0,31) };
#define SLOT_X 200
#define SLOT_Y 140
static int frame = 0;

static u32 rng_state = 12345;
static u32 xrnd(void) {
    rng_state = rng_state * 1103515245 + 12345;
    return (rng_state >> 16) & 0x7FFF;
}

static void draw_title(void) {
    clear(RGB(0,0,10));
    text("DEEPDREAMLINGS", 60, 40, RGB(31,31,0));
    text("DS", 120, 55, RGB(31,31,0));
    text("REAL NEURAL DREAMING", 40, 80, RGB(0,31,31));
    text("PRESS START", 70, 120, RGB(31,31,31));
}

static void draw_select(void) {
    clear(RGB(5,0,10));
    text("CHOOSE YOUR DREAMLING", 30, 10, RGB(31,31,0));
    for (int i = 0; i < 11; i++) {
        int gx = (i % 6) * 40 + 10;
        int gy = (i / 6) * 50 + 30;
        if (i == selected) rect(gx-2, gy-2, 28, 28, RGB(31,31,0));
        draw_sprite_scaled(DREAMLINGS[i], gx, gy, 1, 0);
    }
    text("D-PAD: SELECT  A: CONFIRM", 20, 170, RGB(31,31,31));
}

static void draw_world(void) {
    clear(RGB(0,0,8));
    for (int i = 0; i < 50; i++) {
        int sx = (i * 53) % 256;
        int sy = (i * 97) % 192;
        px(sx, sy, RGB(10,10,15));
    }
    for (int g = 0; g < 3; g++) {
        int gx = gates[g][0], gy = gates[g][1];
        u16 col = gate_colors[g];
        rect(gx, gy, 36, 36, col);
        rect(gx+4, gy+4, 28, 28, RGB(0,0,0));
        for (int i = 0; i < 8; i++) {
            int sx = gx + 18 + ((frame*2 + i*45) % 28) - 14;
            int sy = gy + 18 + ((frame*3 + i*30) % 28) - 14;
            px(sx, sy, RGB(31,31,31));
        }
        char label[8] = {'G','A','T','E',' ','0'+g+1, 0};
        text(label, gx+2, gy+38, RGB(31,31,31));
    }
    rect(SLOT_X, SLOT_Y, 32, 32, RGB(31,31,0));
    rect(SLOT_X+4, SLOT_Y+4, 24, 24, RGB(0,0,0));
    text("SLOT", SLOT_X+2, SLOT_Y+34, RGB(31,31,0));
    draw_sprite_scaled(DREAMLINGS[selected], px_x, py_y, 1, 0);
    text("COLLECTED:", 5, 5, RGB(31,31,31));
    char nc[4]; nc[0]='0'+ncollected/10; nc[1]='0'+ncollected%10; nc[2]='/'; nc[3]=0;
    text(nc, 75, 5, RGB(31,31,0));
    text("11", 95, 5, RGB(31,31,0));
}

static void draw_dream(int iter, int total, int ch) {
    clear(RGB(10,0,15));
    text("DREAMING...", 80, 10, RGB(31,0,31));
    char prog[16];
    prog[0]='0'+iter/10; prog[1]='0'+iter%10; prog[2]='/'; 
    prog[3]='0'+total/10; prog[4]='0'+total%10; prog[5]=0;
    text(prog, 110, 25, RGB(31,31,31));
    draw_sprite_scaled(dream_img, 80, 50, 4, 0);
    text("NEURAL GRADIENT ASCENT", 40, 160, RGB(0,31,31));
}

static void draw_slot(int *reels, int spinning, int stopped) {
    clear(RGB(10,10,0));
    text("DREAM FUSION SLOT", 50, 20, RGB(31,31,0));
    for (int i = 0; i < 3; i++) {
        int rx = 40 + i*60;
        rect(rx, 60, 48, 48, RGB(31,31,31));
        rect(rx+2, 62, 44, 44, RGB(0,0,0));
        if (!spinning || i < stopped) {
            draw_sprite_scaled(DREAMLINGS[reels[i]], rx, 60, 2, 0);
        } else {
            draw_sprite_scaled(DREAMLINGS[(frame+i*3)%11], rx, 60, 2, 0);
        }
    }
    if (spinning) {
        text("SPINNING... A TO STOP", 40, 130, RGB(31,31,31));
    } else {
        if (reels[0]==reels[1] && reels[1]==reels[2]) {
            text("FUSION! NEW DREAMLING!", 30, 130, RGB(0,31,0));
        } else {
            text("NO MATCH. A TO SPIN", 40, 130, RGB(31,31,31));
        }
        text("A: SPIN  B: EXIT", 60, 150, RGB(20,20,20));
    }
}

static void draw_gallery(void) {
    clear(RGB(0,5,10));
    text("DREAMLING GALLERY", 50, 10, RGB(31,31,0));
    int idx = 0;
    for (int i = 0; i < 11; i++) {
        if (!collected[i]) continue;
        int gx = (idx % 6) * 40 + 10;
        int gy = (idx / 6) * 50 + 30;
        draw_sprite_scaled(DREAMLINGS[i], gx, gy, 1, 0);
        idx++;
    }
    for (int i = 0; i < nfused; i++) {
        int gx = (idx % 6) * 40 + 10;
        int gy = (idx / 6) * 50 + 30;
        draw_sprite_scaled(fused[i], gx, gy, 1, RGB(31,0,31));
        idx++;
    }
    text("B: BACK", 90, 180, RGB(20,20,20));
}

int main(void) {
    // Init display like qu33ph (MODE_5_2D with bitmap BG)
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
    int bgMain = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    int bgSub = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    vramReal = bgGetGfxPtr(bgMain);
    vramTop = drawBuf;
    // Enable VBlank IRQ (async DMA may need interrupts enabled)
    irqEnable(IRQ_VBLANK);
    
    int slot_reels[3] = {0,0,0};
    int slot_spinning = 0;
    int slot_stopped = 0;
    int dream_iter = 0;
    int dream_ch = 0;
    int dream_total = 20;
    
    while (1) {
        scanKeys();
        u16 pressed = keysDown();
        u16 held = keysHeld();
        frame++;
        
        if (state == ST_TITLE) {
            draw_title();
            if (pressed & KEY_START) state = ST_SELECT;
        }
        else if (state == ST_SELECT) {
            draw_select();
            if (pressed & KEY_LEFT) selected = (selected + 10) % 11;
            if (pressed & KEY_RIGHT) selected = (selected + 1) % 11;
            if (pressed & KEY_UP) selected = (selected + 5) % 11;
            if (pressed & KEY_DOWN) selected = (selected + 6) % 11;
            if (pressed & KEY_A) {
                if (!collected[selected]) { collected[selected] = 1; ncollected++; }
                state = ST_WORLD;
            }
        }
        else if (state == ST_WORLD) {
            int speed = (held & KEY_B) ? 4 : 2;
            if (held & KEY_LEFT) px_x -= speed;
            if (held & KEY_RIGHT) px_x += speed;
            if (held & KEY_UP) py_y -= speed;
            if (held & KEY_DOWN) py_y += speed;
            if (px_x < 0) px_x = 0; if (px_x > 232) px_x = 232;
            if (py_y < 0) py_y = 0; if (py_y > 168) py_y = 168;
            
            draw_world();
            
            if (pressed & KEY_A) {
                for (int g = 0; g < 3; g++) {
                    int gx = gates[g][0], gy = gates[g][1];
                    if (px_x+12 > gx && px_x < gx+36 && py_y+12 > gy && py_y < gy+36) {
                        for (int i = 0; i < 576; i++) dream_img[i] = DREAMLINGS[selected][i];
                        cnn_forward(dream_img);
                        dream_ch = cnn_best_channel();
                        dream_iter = 0;
                        state = ST_DREAM;
                        break;
                    }
                }
                if (px_x+12 > SLOT_X && px_x < SLOT_X+32 && py_y+12 > SLOT_Y && py_y < SLOT_Y+32) {
                    slot_spinning = 0; slot_stopped = 0;
                    state = ST_SLOT;
                }
            }
            if (pressed & KEY_SELECT) state = ST_GALLERY;
        }
        else if (state == ST_DREAM) {
            if (dream_iter < dream_total) {
                cnn_dream_step(dream_img, dream_ch, 16);
                dream_iter++;
                draw_dream(dream_iter, dream_total, dream_ch);
            } else {
                draw_dream(dream_total, dream_total, dream_ch);
                text("DREAM COMPLETE! A: CONTINUE", 20, 175, RGB(0,31,0));
                if (pressed & KEY_A) state = ST_WORLD;
            }
        }
        else if (state == ST_SLOT) {
            if (!slot_spinning && (pressed & KEY_A)) {
                slot_spinning = 1; slot_stopped = 0;
            }
            if (slot_spinning && (pressed & KEY_A)) {
                slot_stopped++;
                if (slot_stopped >= 3) {
                    slot_spinning = 0;
                    if (slot_reels[0]==slot_reels[1] && slot_reels[1]==slot_reels[2]) {
                        if (nfused < 11) {
                            int a = slot_reels[0];
                            int b = (a+3)%11, c = (a+7)%11;
                            for (int i = 0; i < 576; i++) {
                                int v = (DREAMLINGS[a][i] + DREAMLINGS[b][i] + DREAMLINGS[c][i]) / 3;
                                fused[nfused][i] = (u8)v;
                            }
                            nfused++; ncollected++;
                        }
                    }
                }
            }
            if (slot_spinning) {
                for (int i = slot_stopped; i < 3; i++) slot_reels[i] = xrnd() % 11;
            }
            draw_slot(slot_reels, slot_spinning, slot_stopped);
            if ((pressed & KEY_B) && !slot_spinning) state = ST_WORLD;
        }
        else if (state == ST_GALLERY) {
            draw_gallery();
            if (pressed & KEY_B) state = ST_WORLD;
        }
        
        // DMA copy RAM buffer to VRAM (like qu33ph)
        dmaCopyWords(3, drawBuf, vramReal, 256*192*2);
        swiWaitForVBlank();
    }
    return 0;
}
