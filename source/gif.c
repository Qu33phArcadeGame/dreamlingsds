#include "gif.h"
#include "platform.h"

static char gpath[64];
static int gw, gh;
static u8 *obuf;
static u32 olen, ocap;
static u8 obuf_static[24 * 1024];
static u8 idx[180 * 252];

static void ob(u8 b) { if (olen < ocap) obuf[olen++] = b; }
static void ob16(int v) { ob((u8)(v & 255)); ob((u8)((v >> 8) & 255)); }
static void ostr(const char *s) { while (*s) ob((u8)*s++); }
static bool flush(void) {
    bool ok = plat_file_append(gpath, obuf, olen);
    olen = 0;
    return ok;
}

// palette: 6 red x 7 green x 6 blue = 252 colours (+4 greys)
static int qr(int v5) { return (v5 * 5 + 15) / 31; }
static int qg(int v5) { return (v5 * 6 + 15) / 31; }

bool gif_begin(const char *path, int w, int h) {
    strncpy(gpath, path, sizeof(gpath) - 1);
    gw = w;
    gh = h;
    obuf = obuf_static;
    ocap = sizeof(obuf_static);
    olen = 0;
    if (!plat_file_write(gpath, "", 0)) return false;
    ostr("GIF89a");
    ob16(w);
    ob16(h);
    ob(0xF7); // global colour table, 256 entries
    ob(0);
    ob(0);
    for (int r = 0; r < 6; r++)
        for (int g = 0; g < 7; g++)
            for (int b = 0; b < 6; b++) { ob((u8)(r * 51)); ob((u8)(g * 255 / 6)); ob((u8)(b * 51)); }
    for (int i = 0; i < 4; i++) { ob((u8)(i * 85)); ob((u8)(i * 85)); ob((u8)(i * 85)); }
    // loop forever
    ob(0x21); ob(0xFF); ob(11); ostr("NETSCAPE2.0"); ob(3); ob(1); ob16(0); ob(0);
    return flush();
}

// ---- LZW
static u16 dict_code[4096 * 2];
static s32 dict_key[4096 * 2];
static u32 cur, nbits;
static u8 blk[255];
static int bl;
static void emit(int code, int size) {
    cur |= (u32)code << nbits;
    nbits += size;
    while (nbits >= 8) {
        blk[bl++] = (u8)(cur & 255);
        cur >>= 8;
        nbits -= 8;
        if (bl == 255) { ob(255); for (int i = 0; i < 255; i++) ob(blk[i]); bl = 0; }
    }
}
static int dict_find(s32 key) {
    u32 hsh = ((u32)key * 2654435761u) >> 19; // 13 bits
    while (dict_key[hsh] != -1) {
        if (dict_key[hsh] == key) return dict_code[hsh];
        hsh = (hsh + 1) & 8191;
    }
    return -1;
}
static void dict_put(s32 key, int code) {
    u32 hsh = ((u32)key * 2654435761u) >> 19;
    while (dict_key[hsh] != -1) hsh = (hsh + 1) & 8191;
    dict_key[hsh] = key;
    dict_code[hsh] = (u16)code;
}

bool gif_frame(const u16 *px, int delay_cs) {
    int n = gw * gh;
    for (int i = 0; i < n; i++) {
        u16 c = px[i];
        int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
        idx[i] = (u8)(qr(r) * 42 + qg(g) * 6 + qr(b));
    }
    ob(0x21); ob(0xF9); ob(4); ob(0x04); ob16(delay_cs); ob(0); ob(0);
    ob(0x2C); ob16(0); ob16(0); ob16(gw); ob16(gh); ob(0);
    const int min = 8, clear = 256, eoi = 257;
    ob((u8)min);
    int size = min + 1, next = eoi + 1;
    for (int i = 0; i < 8192; i++) dict_key[i] = -1;
    cur = 0; nbits = 0; bl = 0;
    emit(clear, size);
    int prefix = idx[0];
    for (int i = 1; i < n; i++) {
        int k = idx[i];
        s32 key = (prefix << 8) | k;
        int v = dict_find(key);
        if (v >= 0) { prefix = v; continue; }
        emit(prefix, size);
        if (next == 4096) {
            emit(clear, size);
            for (int j = 0; j < 8192; j++) dict_key[j] = -1;
            next = eoi + 1;
            size = min + 1;
        } else {
            if (next >= (1 << size)) size++;
            dict_put(key, next++);
        }
        prefix = k;
        if (olen > ocap - 2048 && !flush()) return false;
    }
    emit(prefix, size);
    emit(eoi, size);
    if (nbits > 0) blk[bl++] = (u8)(cur & 255);
    if (bl) { ob((u8)bl); for (int i = 0; i < bl; i++) ob(blk[i]); }
    ob(0);
    return flush();
}

bool gif_end(void) {
    ob(0x3B);
    return flush();
}
