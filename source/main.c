#include <nds.h>
static volatile int vbCount = 0;
void vbIrq(void) { vbCount++; }
int main(void) {
    videoSetMode(MODE_5_2D); videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG); vramSetBankC(VRAM_C_SUB_BG);
    int bgm = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    int bgs = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    u16 *vramBot = bgGetGfxPtr(bgm);
    u16 *vramTop = bgGetGfxPtr(bgs);
    lcdMainOnBottom();
    irqSet(IRQ_VBLANK, vbIrq); irqEnable(IRQ_VBLANK);
    for (int i = 0; i < 256*256; i++) {
        vramBot[i] = RGB15(31, 0, 0);
        vramTop[i] = RGB15(0, 0, 31);
    }
    while (1) { swiWaitForVBlank(); }
    return 0;
}
