#pragma once
void cnn_forward(const unsigned char *input);
int cnn_best_channel(void);
void cnn_dream_step(unsigned char *input, int ch, int step);
