/*
 * Faces are pixel-exact bitmaps exported from the Figma file
 * "BMO Face Templates/Assets (Community)" (see tools/gen_faces.py), brought to
 * life at runtime: each face has its own idle motion (bounce, shake, sway,
 * heart-beat...), eyes blink, and a face change squashes and "pops" like a
 * cartoon. Everything is a cheap affine remap of the bitmap, no extra assets.
 */
#include "faces.h"

#include <math.h>
#include <string.h>

#include "faces_data.h"
#include "gfx.h"

#define BG RGB565(FACE_BG_R, FACE_BG_G, FACE_BG_B)
#define PI_F 3.14159265f

typedef enum { M_BOB, M_BOUNCE, M_SHAKE, M_SWAY, M_PULSE, M_BEAT, M_WOBBLE, M_LAUGH } motion_t;

/* Motion per face, in Rosto-01 .. Rosto-25 order. */
static const uint8_t kMotion[FACE_COUNT] = {
    M_BOB,    /* 01 smile     */ M_BOUNCE, /* 02 open smile */
    M_SWAY,   /* 03 sad       */ M_BOB,    /* 04 happy eyes */
    M_SWAY,   /* 05 blush     */ M_PULSE,  /* 06 teary      */
    M_SHAKE,  /* 07 angry     */ M_SWAY,   /* 08 bored      */
    M_SHAKE,  /* 09 grumpy    */ M_SHAKE,  /* 10 furious    */
    M_LAUGH,  /* 11 laugh-cry */ M_LAUGH,  /* 12 sobbing    */
    M_BOB,    /* 13 kawaii    */ M_BOB,    /* 14 tongue     */
    M_SWAY,   /* 15 suspicious*/ M_SWAY,   /* 16 sleepy     */
    M_WOBBLE, /* 17 dizzy     */ M_BOB,    /* 18 content    */
    M_BOUNCE, /* 19 ><3       */ M_PULSE,  /* 20 stars      */
    M_BEAT,   /* 21 hearts    */ M_BOB,    /* 22 round eyes */
    M_PULSE,  /* 23 sparkle   */ M_WOBBLE, /* 24 spiral     */
    M_SHAKE,  /* 25 X_X       */
};

typedef struct {
    float dx, dy; /* pixels */
    float sx, sy; /* scale around the panel centre */
} xform_t;

static color_t scratch[SCR_W * SCR_H];
static int16_t xmap[SCR_W];

static xform_t idle_motion(int id, uint32_t t_ms) {
    float t = (float)t_ms;
    float a = face_wide[id] ? 0.5f : 1.f; /* wide faces must not clip */
    xform_t m = {0.f, 0.f, 1.f, 1.f};
    switch (kMotion[id]) {
    case M_BOB: {
        float s = sinf(t / 420.f);
        m.dy = s * 2.2f * a;
        m.sy = 1.f + 0.022f * s * a;
        m.sx = 1.f - 0.012f * s * a;
        break;
    }
    case M_BOUNCE: { /* hop: squash on the ground, stretch in the air */
        float h = fabsf(sinf(t / 230.f));
        float g = (1.f - h) * (1.f - h);
        m.dy = -h * 5.f * a;
        m.sy = 1.f + (0.05f * h - 0.07f * g) * a;
        m.sx = 1.f - (0.03f * h - 0.06f * g) * a;
        break;
    }
    case M_SHAKE:
        m.dx = (sinf(t / 17.f) + 0.6f * sinf(t / 7.3f)) * 1.1f * a;
        m.dy = sinf(t / 23.f) * 0.7f * a;
        break;
    case M_SWAY: {
        float s = sinf(t / 850.f);
        m.dx = s * 3.f * a;
        m.dy = (1.f - cosf(t / 850.f * 2.f)) * 0.8f * a; /* sag at the extremes */
        m.sy = 1.f + 0.012f * s * s * a;
        break;
    }
    case M_PULSE: {
        float s = sinf(t / 240.f);
        m.sx = m.sy = 1.f + 0.045f * s * a;
        break;
    }
    case M_BEAT: { /* lub-dub */
        float p = fmodf(t, 900.f) / 900.f, k = 0.f;
        if (p < 0.12f) k = sinf(p / 0.12f * PI_F);
        else if (p > 0.22f && p < 0.36f) k = 0.6f * sinf((p - 0.22f) / 0.14f * PI_F);
        m.sx = m.sy = 1.f + 0.07f * k * a;
        break;
    }
    case M_WOBBLE:
        m.dx = sinf(t / 310.f) * 2.2f * a;
        m.dy = cosf(t / 380.f) * 2.f * a;
        m.sx = 1.f + 0.02f * sinf(t / 230.f) * a;
        break;
    case M_LAUGH:
        m.dy = sinf(t / 55.f) * 1.4f * a + sinf(t / 600.f) * 1.5f * a;
        m.dx = sinf(t / 90.f) * 0.8f * a;
        m.sy = 1.f + 0.02f * sinf(t / 55.f) * a;
        break;
    }
    return m;
}

/* Squash the eye boxes vertically into `scratch`, leaving the rest as is. */
static const color_t *apply_blink(const color_t *src, const uint8_t *eye, float open) {
    if (open < 0.08f) open = 0.08f;
    memcpy(scratch, src, sizeof scratch);
    for (int k = 0; k < 2; k++) {
        const uint8_t *e = eye + 4 * k;
        int x0 = e[0], y0 = e[1], w = e[2], h = e[3];
        float cy = (float)y0 + (float)h * 0.5f;
        for (int y = y0; y < y0 + h; y++) {
            int sy = (int)floorf(cy + ((float)y + 0.5f - cy) / open);
            color_t *dst = &scratch[y * SCR_W + x0];
            if (sy < y0 || sy >= y0 + h) {
                for (int x = 0; x < w; x++) dst[x] = BG;
            } else {
                memcpy(dst, &src[sy * SCR_W + x0], (size_t)w * sizeof(color_t));
            }
        }
    }
    return scratch;
}

void face_draw(int id, uint32_t t_ms, float squash, float blink, uint32_t since_swap) {
    id = ((id % FACE_COUNT) + FACE_COUNT) % FACE_COUNT;
    const color_t *src = face_bitmaps[id];
    const uint8_t *eye = face_eyes[id];
    if (blink < 0.98f && eye[2] && eye[6]) src = apply_blink(src, eye, blink);

    xform_t m = idle_motion(id, t_ms);

    /* Spring "pop" when a face arrives: overshoot taller, settle. */
    if (since_swap < 450) {
        float t = (float)since_swap;
        float k = expf(-t / 110.f) * cosf(t / 48.f);
        m.sy *= 1.f + 0.09f * k;
        m.sx *= 1.f - 0.06f * k;
    }
    if (squash < 0.999f) {
        if (squash < 0.04f) squash = 0.04f;
        m.sy *= squash;
        m.sx *= 1.f + (1.f - squash) * 0.12f;
    }

    color_t *fb = gfx_fb();
    const float cx = SCR_W * 0.5f, cy = SCR_H * 0.5f;
    if (fabsf(m.dx) < 0.3f && fabsf(m.dy) < 0.3f && fabsf(m.sx - 1.f) < 0.004f &&
        fabsf(m.sy - 1.f) < 0.004f) {
        memcpy(fb, src, sizeof scratch);
        return;
    }
    for (int x = 0; x < SCR_W; x++) {
        int sx = (int)floorf((((float)x + 0.5f - cx - m.dx) / m.sx) + cx);
        xmap[x] = (int16_t)((sx < 0 || sx >= SCR_W) ? -1 : sx);
    }
    for (int y = 0; y < SCR_H; y++) {
        int sy = (int)floorf((((float)y + 0.5f - cy - m.dy) / m.sy) + cy);
        color_t *dst = &fb[y * SCR_W];
        if (sy < 0 || sy >= SCR_H) {
            for (int x = 0; x < SCR_W; x++) dst[x] = BG;
            continue;
        }
        const color_t *row = &src[sy * SCR_W];
        for (int x = 0; x < SCR_W; x++) dst[x] = xmap[x] < 0 ? BG : row[xmap[x]];
    }
}
