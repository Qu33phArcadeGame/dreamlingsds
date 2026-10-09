// Deepdreamlings DS - game state shared by all screens
#pragma once
#include "common.h"
#include "gfx.h"
#include "assets.h"
#include "platform.h"

#define MAX_FUSIONS 240
#define MAX_COMP 8

typedef struct {
    u8 comp[MAX_COMP];   // dreamling numbers (1..NUM_DREAMLINGS)
    u8 tier[MAX_COMP];   // TIER_* of each part
    u8 n;                // number of parts
    u8 rare;             // 0 none, 1 half, 2 full
    u16 power;           // sum of parts
} Fusion;

// what a card / companion shows: a single dreamling or a fusion
typedef struct {
    u8 kind;             // 0 none, 1 dreamling, 2 fusion
    u8 num;              // dreamling number (kind 1)
    u8 tier;             // dreamling tier (kind 1)
    u16 fus;             // fusion index (kind 2)
} Pick;

// Dream Generator settings (same knobs as the website)
enum { PATH_STRAIGHT, PATH_SQUARE, PATH_TRIANGLE };
typedef struct {
    u8 path;
    u8 frames;        // frames per Zoom press
    u16 zoom;         // crop factor x10000 (9700 = 0.97)
    u8 steps;         // steps per frame
    u16 lr;           // learning rate x1000 (40 = 0.040)
    u8 range;         // movement range x1000 (80 = 0.08)
    u8 seg;           // frames per side
    u8 start_steps;   // starting dream steps per octave
    u8 sharpen;       // 0..100
    u8 layers;        // DN_LO | DN_MID | DN_HI
    u8 groups;        // bit mask of the 4 groups (15 = all)
    u8 detail;        // 0 = 40x50, 1 = 80x100
    u8 play_fps;      // zoom-in playback speed
    u8 play_fps_out;  // zoom-out playback speed
    u8 boomerang;     // zoomerang on/off
    u8 book;          // generator held sideways like a book
    u8 left_handed;   // book mode with the touch screen on the left
    u8 pad_hint_seen; // shown the "turn your DS sideways" tip
} GenSettings;

#define SAVE_MAGIC 0x444C4453u  // "SDLD"
#define SAVE_VERSION 3
typedef struct {
    u32 magic, version;
    u8 caught[NUM_DREAMLINGS + 1];    // seen in a fusion
    u8 tiers[NUM_DREAMLINGS + 1];     // bit mask of owned tiers
    u16 nfus;
    Fusion fus[MAX_FUSIONS];
    Pick companion;
    u8 map, px, py, face;
    u8 water_said, tut_done, bob_talks, pad;
    u16 card_no;                      // Dream Generator card counter
    u16 kept;                         // kept dream cards on the SD card
    GenSettings gen;
    u32 play_seconds;
} SaveData;

extern SaveData G;

// ---------------------------------------------------------------- modes
enum { MODE_TITLE, MODE_WORLD, MODE_ENCOUNTER, MODE_BINDER, MODE_GENERATOR };
void set_mode(int m);
extern int game_mode;
extern u32 frame_no;

// areas: which dreamlings live where and which dream model each generator runs
typedef struct {
    const char *name;
    u8 first, last;     // dreamling range
    u8 model;           // index into dn_models (v1, v2, v3)
    u8 random_enc;      // 0 = encounter when stepping into the zone, else 1-in-N chance per step
    s8 gen_x, gen_y;    // Dream Generator building (top-left tile), -1 = none
    u16 tint;           // background tint for encounters
    u8 slots;           // reels in the dream slots = dreamlings fused per spin
} Area;
const Area *area_for_map(int map);

// ---------------------------------------------------------------- names & drawing
void dreamling_name(int num, char *out);
void fusion_name(const Fusion *f, char *out);
const char *fusion_label(const Fusion *f);
int fusion_best_tier(const Fusion *f);
// creature sprite centred at (cx, cy), size in pixels
void draw_dreamling(Surf *s, int num, int tier, int cx, int cy, int size, int alpha);
void draw_fusion(Surf *s, const Fusion *f, int cx, int cy, int size);
void draw_pick(Surf *s, const Pick *p, int cx, int cy, int size);
void draw_tier_dots(Surf *s, const u8 *tiers, int n, int cx, int cy);
// card art: the creature in front of its dream photo (w x h RGB15)
void make_card_art(const Pick *p, u16 *out, int w, int h);
void pick_name(const Pick *p, char *out);
void pick_detail(const Pick *p, char *out);
int pick_rarity(const Pick *p); // 0 common, 1 rare, 2 holo

// ---------------------------------------------------------------- save
void save_defaults(void);
bool save_load(void);
bool save_write(void);
void add_fusion(const Fusion *f);
extern bool save_dirty;

// ---------------------------------------------------------------- screens
void world_enter(void);
void world_update(void);
void world_resume(void);
void world_say(const char *t);
void encounter_start(void);
void encounter_update(void);
void binder_open(int return_mode);
void binder_update(void);
void generator_open(int area_model);
void generator_update(void);

// ---------------------------------------------------------------- floating dreams (RAM only)
int floater_begin(int map, const Pick *p, int x, int y);   // returns the slot
void floater_frame(int slot, const u16 *frame, int w, int h);
void floater_end(int slot);
int floaters_in_town(int map);
void floaters_draw(Surf *s, int map, int camx, int camy, int map_w, int map_h, int pcx, int pcy);

// ---------------------------------------------------------------- UI helpers (bottom screen)
typedef struct { s16 x, y, w, h; } Rect;
bool ui_button(Surf *s, Rect r, const char *label, u16 fill, bool enabled); // draws; returns tapped
bool ui_tapped(Rect r);
void ui_set_touch_xform(int mode); // 0 normal, 1 book right-handed, 2 book left-handed
extern int touch_x, touch_y;       // in UI coordinates
extern bool touch_down, touch_held;
void ui_begin_frame(void);
void draw_bg_pattern(Surf *s, int t, u16 a, u16 b);
void mini_text(Surf *s, const char *t, int x, int y, u16 c); // 3x5 font, 4px per char
int mini_w(const char *t);
