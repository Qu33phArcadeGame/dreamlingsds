// Trading card renderer (180 x 252, the hand-drawn pixel frame over holo foil)
#pragma once
#include "game.h"

#define CARD_W 180
#define CARD_H 252
#define ART_X 10
#define ART_Y 24
#define ART_W 160
#define ART_H 200

typedef struct {
    char name[32];
    char detail[32];
    char style[48];
    u8 rarity;          // 0 common, 1 rare, 2 holo
    u16 number;         // 0 = not dreamed yet
    u16 frames;         // frame-count badge (0/1 = hidden)
    char bullets[6][64];
    u8 nbullets;
} CardInfo;

// Render the card into dst (CARD_W x CARD_H). art = aw x ah RGB15 picture for the window.
void card_render(u16 *dst, const CardInfo *ci, const u16 *art, int aw, int ah, int t);
// Draw a rendered card onto a surface at 3/4 size (135 x 189).
void card_draw_small(Surf *s, const u16 *card, int x, int y);
// Draw a rendered card rotated for "book" mode onto a 256x192 screen (fills it).
void card_draw_book(Surf *s, const u16 *card, bool left_handed);
// shared scratch buffers (one card and one portrait page at a time)
extern u16 card_shared[CARD_W * CARD_H];
extern u16 portrait_shared[192 * 256];
// rotate a 192x256 portrait buffer onto a 256x192 screen (book mode)
void blit_portrait(Surf *s, const u16 *portrait, bool left_handed);
