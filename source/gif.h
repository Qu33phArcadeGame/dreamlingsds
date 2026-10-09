// Tiny animated-GIF writer (fixed 6x7x6 colour cube, LZW) that streams to the SD card
#pragma once
#include "common.h"

bool gif_begin(const char *path, int w, int h);
bool gif_frame(const u16 *rgb15, int delay_cs);
bool gif_end(void);
