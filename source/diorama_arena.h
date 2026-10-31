/* Shared diorama arena logic for Dreamlings Wii (full) and DS (lite).
 * Platform-free: no Wii, DS, or Three.js headers. Both renderers consume
 * the box list below, so the game logic, CNN, and world layout stay
 * identical and only the renderer (GX vs DS GL vs host test) differs.
 *
 * Coordinate system: arena units, not pixels.
 *   X: left -> right, 0..16
 *   Z: top -> bottom on the DS 2D map, 0..12  (front of the diorama is +Z)
 *   Y: up, 0 = floor top. Boxes sit on Y=0 unless noted.
 * This matches the DS world (256x192, gates at 30,40 / 190,40 / 110,130,
 * slot at 200,140) rescaled by 16 px per arena unit:
 *   gate1 (1.9,2.5), gate2 (11.9,2.5), gate3 (6.9,8.1), slot (12.5,8.75)
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float x, z;       /* centre, arena units */
    float w, h, d;    /* size, arena units (h is height in Y) */
    unsigned char r, g, b;
    int kind;         /* DIO_* below */
} DioBox;

enum {
    DIO_BASE = 0,
    DIO_FLOOR,
    DIO_GATE,
    DIO_SLOT,
    DIO_TREE_TRUNK,
    DIO_TREE_CANOPY,
    DIO_WATER
};

/* Visible-face masks: a box drawn in full is 12 triangles (6 faces x 2).
 * Front-view diorama cameras only ever see top + front (+ one side),
 * so both renderers submit visible faces only and count them here. */
#define DIO_FACE_TOP   1
#define DIO_FACE_FRONT 2
#define DIO_FACE_SIDE  4
#define DIO_FACE_ALL   7

static inline int dio_faces_triangles(int faces) {
    int n = 0;
    if (faces & DIO_FACE_TOP) n += 2;
    if (faces & DIO_FACE_FRONT) n += 2;
    if (faces & DIO_FACE_SIDE) n += 2;
    return n;
}

/* Wii full diorama: voxel floor (one box per floor cell), base slab,
 * 3 tall gate boxes, slot cabinet, 4 trees (trunk + canopy each).
 * Floor cells: 16 x 12 = 192. Gates/slot/trees are decorative props
 * around the DS arena; gameplay collision still uses the DS gate/slot
 * rectangles, not these boxes. */
static inline int dio_build_wii_boxes(DioBox *out, int max_boxes) {
    int n = 0;
    if (max_boxes < 1) return 0;
    /* Wooden diorama base, slightly larger than the floor. */
    out[n++] = (DioBox){8.0f, 6.0f, 17.0f, 0.8f, 13.0f, 0x5a, 0x3a, 0x28, DIO_BASE};
    /* Voxel floor: ground height 0.35, like the web diorama. */
    for (int r = 0; r < 12 && n < max_boxes; r++) {
        for (int c = 0; c < 16 && n < max_boxes; c++) {
            out[n++] = (DioBox){c + 0.5f, r + 0.5f, 1.0f, 0.35f, 1.0f,
                                0x5a, 0xa8, 0x4a, DIO_FLOOR};
        }
    }
    /* Gates: tall boxes (height 2.25, matching the DS 36px gate). */
    static const float gx[3] = {1.9f, 11.9f, 6.9f};
    static const float gz[3] = {2.5f, 2.5f, 8.1f};
    static const unsigned char gc[3][3] = {{31,0,0},{0,31,0},{0,0,31}};
    for (int g = 0; g < 3 && n < max_boxes; g++) {
        out[n++] = (DioBox){gx[g] + 1.0f, gz[g] + 1.0f, 2.25f, 2.25f, 2.25f,
                            (unsigned char)(gc[g][0]*8),
                            (unsigned char)(gc[g][1]*8),
                            (unsigned char)(gc[g][2]*8), DIO_GATE};
    }
    /* Slot cabinet. */
    if (n < max_boxes)
        out[n++] = (DioBox){13.5f, 9.75f, 2.0f, 2.0f, 2.0f, 0xf8, 0xd8, 0x00, DIO_SLOT};
    /* 4 corner trees: trunk 1.0 high, canopy 1.4 high on top of it. */
    static const float tx[4] = {0.5f, 15.5f, 0.5f, 15.5f};
    static const float tz[4] = {0.5f, 0.5f, 11.5f, 11.5f};
    for (int t = 0; t < 4 && n + 2 <= max_boxes; t++) {
        out[n++] = (DioBox){tx[t], tz[t], 0.5f, 1.0f, 0.5f, 0x6b, 0x42, 0x1a, DIO_TREE_TRUNK};
        out[n++] = (DioBox){tx[t], tz[t], 1.5f, 1.4f, 1.5f, 0x2e, 0x7d, 0x32, DIO_TREE_CANOPY};
    }
    return n;
}

/* DS diorama-lite: NO per-tile floor. One flat floor slab, one base slab,
 * gates and slot as boxes with visible faces only, player as a billboard
 * (2 triangles, counted by the caller, not in this list).
 * Total box triangles (visible faces only) must stay far under 2048.
 * NOTE: player billboards in both renderers are drawn pixel-by-pixel
 * (576 quads max, 1152 triangles), not as a single quad, to avoid
 * texture-format pitfalls. Budgets below include that worst case. */
static inline int dio_build_ds_boxes(DioBox *out, int max_boxes) {
    int n = 0;
    if (max_boxes < 5) return 0;
    out[n++] = (DioBox){8.0f, 6.0f, 17.0f, 0.8f, 13.0f, 0x5a, 0x3a, 0x28, DIO_BASE};
    out[n++] = (DioBox){8.0f, 6.0f, 16.0f, 0.35f, 12.0f, 0x5a, 0xa8, 0x4a, DIO_FLOOR};
    static const float gx[3] = {1.9f, 11.9f, 6.9f};
    static const float gz[3] = {2.5f, 2.5f, 8.1f};
    static const unsigned char gc[3][3] = {{31,0,0},{0,31,0},{0,0,31}};
    for (int g = 0; g < 3 && n < max_boxes; g++) {
        out[n++] = (DioBox){gx[g] + 1.0f, gz[g] + 1.0f, 2.25f, 2.25f, 2.25f,
                            (unsigned char)(gc[g][0]*8),
                            (unsigned char)(gc[g][1]*8),
                            (unsigned char)(gc[g][2]*8), DIO_GATE};
    }
    if (n < max_boxes)
        out[n++] = (DioBox){13.5f, 9.75f, 2.0f, 2.0f, 2.0f, 0xf8, 0xd8, 0x00, DIO_SLOT};
    return n;
}

/* Triangle budget for a box list, front-view visible faces only.
 * Base/floor: top only (camera looks down slightly, sides are hidden by
 * the base edge in lite mode, and are edge-only in full mode -- counted
 * conservatively as top + front here). Gates/slot/trees: top + front. */
static inline int dio_triangle_budget(const DioBox *boxes, int count) {
    int tris = 0;
    for (int i = 0; i < count; i++) {
        int faces = DIO_FACE_TOP;
        if (boxes[i].kind == DIO_GATE || boxes[i].kind == DIO_SLOT ||
            boxes[i].kind == DIO_TREE_TRUNK || boxes[i].kind == DIO_TREE_CANOPY)
            faces |= DIO_FACE_FRONT;
        tris += dio_faces_triangles(faces);
    }
    return tris;
}

#ifdef __cplusplus
}
#endif
