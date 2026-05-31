#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspctrl.h>
#include <pspaudio.h>
#include <pspaudiolib.h>
#include <pspmp3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

PSP_MODULE_INFO("RetroViz", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(20480);

#define SCREEN_W 480
#define SCREEN_H 272
#define NUM_MODES 8
#define NUM_BARS 28

// --- Framebuffer ---
static unsigned int __attribute__((aligned(16))) list[262144];
static unsigned short* vram = (unsigned short*)0x44000000;

// --- Bars ---
static float bars[NUM_BARS];
static float bar_targets[NUM_BARS];
static float bar_peaks[NUM_BARS];

// --- State ---
static int current_mode = 0;
static int running = 1;
static unsigned int frame_tick = 0;

// --- Track info (from ID3 or filename) ---
static char track_name[128] = "NO NAME";
static char artist_name[64]  = "UNKNOWN";
static int  track_num        = 1;
static int  track_seconds    = 0;

// ============================================================
// Exit callback
// ============================================================
int exit_callback(int arg1, int arg2, void *common) { running = 0; return 0; }
int callback_thread(SceSize args, void *argp) {
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}
void setup_callbacks(void) {
    int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0) sceKernelStartThread(thid, 0, 0);
}

// ============================================================
// Drawing helpers — direct framebuffer pixel writes
// ============================================================
static inline void put_pixel(int x, int y, unsigned short color) {
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
    vram[y * 512 + x] = color;
}

// RGB888 -> RGB565
static inline unsigned short rgb(int r, int g, int b) {
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

static void fill_rect(int x, int y, int w, int h, unsigned short color) {
    for (int dy = 0; dy < h; dy++)
        for (int dx = 0; dx < w; dx++)
            put_pixel(x + dx, y + dy, color);
}

static void draw_line(int x0, int y0, int x1, int y1, unsigned short color) {
    int dx = abs(x1-x0), sx = x0<x1?1:-1;
    int dy = -abs(y1-y0), sy = y0<y1?1:-1;
    int err = dx+dy, e2;
    while (1) {
        put_pixel(x0, y0, color);
        if (x0==x1 && y0==y1) break;
        e2 = 2*err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void draw_ellipse(int cx, int cy, int rx, int ry, unsigned short color) {
    for (int angle = 0; angle < 360; angle++) {
        float a = angle * 3.14159f / 180.0f;
        int x = (int)(cx + rx * cosf(a));
        int y = (int)(cy + ry * sinf(a));
        put_pixel(x, y, color);
    }
}

// Simple 5x7 bitmap font (ASCII 32-127)
// Minimal — only digits, letters, colon, slash, space
static const unsigned char font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // space
    {0x00,0x00,0x5F,0x00,0x00}, // !
    {0x00,0x07,0x00,0x07,0x00}, // "
    {0x14,0x7F,0x14,0x7F,0x14}, // #
    {0x24,0x2A,0x7F,0x2A,0x12}, // $
    {0x23,0x13,0x08,0x64,0x62}, // %
    {0x36,0x49,0x55,0x22,0x50}, // &
    {0x00,0x05,0x03,0x00,0x00}, // '
    {0x00,0x1C,0x22,0x41,0x00}, // (
    {0x00,0x41,0x22,0x1C,0x00}, // )
    {0x14,0x08,0x3E,0x08,0x14}, // *
    {0x08,0x08,0x3E,0x08,0x08}, // +
    {0x00,0x50,0x30,0x00,0x00}, // ,
    {0x08,0x08,0x08,0x08,0x08}, // -
    {0x00,0x60,0x60,0x00,0x00}, // .
    {0x20,0x10,0x08,0x04,0x02}, // /
    {0x3E,0x51,0x49,0x45,0x3E}, // 0
    {0x00,0x42,0x7F,0x40,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4B,0x31}, // 3
    {0x18,0x14,0x12,0x7F,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 6
    {0x01,0x71,0x09,0x05,0x03}, // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {0x06,0x49,0x49,0x29,0x1E}, // 9
    {0x00,0x36,0x36,0x00,0x00}, // :
    {0x00,0x56,0x36,0x00,0x00}, // ;
    {0x08,0x14,0x22,0x41,0x00}, // <
    {0x14,0x14,0x14,0x14,0x14}, // =
    {0x00,0x41,0x22,0x14,0x08}, // >
    {0x02,0x01,0x51,0x09,0x06}, // ?
    {0x32,0x49,0x79,0x41,0x3E}, // @
    {0x7E,0x11,0x11,0x11,0x7E}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x3E,0x41,0x41,0x41,0x22}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x41}, // E
    {0x7F,0x09,0x09,0x09,0x01}, // F
    {0x3E,0x41,0x49,0x49,0x7A}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x0C,0x02,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x3E,0x41,0x41,0x41,0x3E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x3E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x3F,0x40,0x38,0x40,0x3F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x07,0x08,0x70,0x08,0x07}, // Y
    {0x61,0x51,0x49,0x45,0x43}, // Z
};

static void draw_char(int x, int y, char c, unsigned short color, int scale) {
    if (c < 32 || c > 90) c = 32;
    int idx = c - 32;
    for (int col = 0; col < 5; col++) {
        unsigned char bits = font5x7[idx][col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                fill_rect(x + col*scale, y + row*scale, scale, scale, color);
            }
        }
    }
}

static void draw_text(int x, int y, const char* str, unsigned short color, int scale) {
    int cx = x;
    while (*str) {
        char c = *str++;
        if (c >= 'a' && c <= 'z') c -= 32; // uppercase
        draw_char(cx, y, c, color, scale);
        cx += (5 + 1) * scale;
    }
}

// ============================================================
// Fake audio bars (simulated — real MP3 needs pspmp3 callbacks)
// ============================================================
static void update_bars(void) {
    if ((frame_tick % 8) == 0) {
        for (int i = 0; i < NUM_BARS; i++) {
            float center = NUM_BARS / 2.0f;
            float dist = fabsf(i - center) / center;
            bar_targets[i] = (0.2f + ((float)rand()/RAND_MAX) * 0.75f - dist * 0.2f) * 120.0f;
        }
    }
    for (int i = 0; i < NUM_BARS; i++) {
        if (bars[i] < bar_targets[i]) bars[i] = fminf(bars[i] + 6.0f, bar_targets[i]);
        else bars[i] = fmaxf(bars[i] - 3.0f, 0.0f);
        if (bars[i] > bar_peaks[i]) bar_peaks[i] = bars[i];
        else bar_peaks[i] = fmaxf(0.0f, bar_peaks[i] - 0.4f);
    }
}

// ============================================================
// Draw common track info footer
// ============================================================
static void draw_track_info(void) {
    unsigned short cy = rgb(255, 204, 0);
    unsigned short cb = rgb(0, 170, 204);
    unsigned short cdim = rgb(40, 40, 40);

    draw_text(10, SCREEN_H - 22, track_name, cy, 1);
    draw_text(10, SCREEN_H - 12, artist_name, cb, 1);

    // progress bar
    fill_rect(SCREEN_W - 92, SCREEN_H - 18, 82, 3, cdim);
    fill_rect(SCREEN_W - 92, SCREEN_H - 18, 50, 3, rgb(0, 119, 187));

    // track number
    char buf[16];
    snprintf(buf, sizeof(buf), "TR %02d", track_num);
    draw_text(SCREEN_W - 92, SCREEN_H - 9, buf, cdim, 1);
}

// ============================================================
// MODE 0: Classic EQ bars (Alpine style)
// ============================================================
static void draw_mode0(void) {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, rgb(0, 13, 26));

    int bw = 12, gap = 4;
    int total = NUM_BARS * (bw + gap) - gap;
    int sx = (SCREEN_W - total) / 2;
    int base_y = SCREEN_H - 42;

    for (int i = 0; i < NUM_BARS; i++) {
        int x = sx + i * (bw + gap);
        int bh = (int)bars[i];
        int segs = bh / 6;
        for (int j = 0; j < segs; j++) {
            int sy = base_y - j * 7;
            unsigned short col;
            if (j > segs * 85 / 100)      col = rgb(255, 34, 0);
            else if (j > segs * 65 / 100) col = rgb(255, 170, 0);
            else                           col = rgb(0, 136, 238);
            fill_rect(x, sy, bw, 5, col);
        }
        if (bar_peaks[i] > 4) {
            int py = base_y - (int)bar_peaks[i];
            fill_rect(x, py, bw, 2, rgb(255, 255, 255));
        }
    }
    fill_rect(0, base_y + 2, SCREEN_W, 1, rgb(0, 51, 85));
    draw_track_info();
}

// ============================================================
// MODE 1: Oscilloscope
// ============================================================
static void draw_mode1(void) {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, rgb(0, 13, 26));

    // grid
    for (int x = 0; x < SCREEN_W; x += 40)
        draw_line(x, 0, x, SCREEN_H - 45, rgb(0, 30, 20));
    for (int y = 0; y < SCREEN_H - 45; y += 20)
        draw_line(0, y, SCREEN_W, y, rgb(0, 30, 20));

    int mid_y = (SCREEN_H - 45) / 2;
    float avg = 0;
    for (int i = 0; i < NUM_BARS; i++) avg += bars[i];
    avg /= NUM_BARS;
    float amp = avg * 0.4f + 15.0f;

    int prev_y = mid_y;
    for (int x = 0; x < SCREEN_W; x++) {
        float phase = x * 0.04f + frame_tick * 0.05f;
        float y_f = mid_y
            + sinf(phase) * amp
            + sinf(phase * 1.7f) * amp * 0.4f
            + sinf(phase * 3.1f) * 6.0f;
        int y = (int)y_f;
        if (x > 0) draw_line(x - 1, prev_y, x, y, rgb(0, 255, 136));
        prev_y = y;
    }
    draw_track_info();
}

// ============================================================
// MODE 2: Circular spectrum
// ============================================================
static void draw_mode2(void) {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, rgb(0, 13, 26));
    int cx = SCREEN_W / 2, cy = (SCREEN_H - 45) / 2;

    int radii[] = {40, 60, 80, 100};
    for (int r = 0; r < 4; r++)
        draw_ellipse(cx, cy, radii[r], radii[r], rgb(0, 17, 51));

    int slices = 48;
    for (int i = 0; i < slices; i++) {
        float angle = (float)i / slices * 2.0f * 3.14159f - 3.14159f / 2.0f;
        int bi = i * NUM_BARS / slices;
        float blen = bars[bi] * 0.55f + 16.0f;
        float ratio = bars[bi] / 120.0f;

        int x1 = (int)(cx + cosf(angle) * 38);
        int y1 = (int)(cy + sinf(angle) * 38);
        int x2 = (int)(cx + cosf(angle) * (38 + blen * 0.65f));
        int y2 = (int)(cy + sinf(angle) * (38 + blen * 0.65f));

        unsigned short col;
        if (ratio > 0.75f)      col = rgb(255, 51, 0);
        else if (ratio > 0.5f)  col = rgb(255, 170, 0);
        else                    col = rgb(0, 153, 255);
        draw_line(x1, y1, x2, y2, col);
    }
    draw_text(cx - 20, cy - 4, "SPEC", rgb(0, 170, 255), 1);
    draw_track_info();
}

// ============================================================
// MODE 3: VU meters
// ============================================================
static float vu_l = 0, vu_r = 0, vu_lp = 0, vu_rp = 0;
static float vu_tl = 0.6f, vu_tr = 0.5f;

static void draw_mode3(void) {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, rgb(0, 13, 26));

    if ((frame_tick % 12) == 0) {
        vu_tl = 0.3f + ((float)rand()/RAND_MAX) * 0.65f;
        vu_tr = 0.3f + ((float)rand()/RAND_MAX) * 0.65f;
    }
    vu_l = vu_l < vu_tl ? fminf(vu_l + 0.04f, vu_tl) : fmaxf(vu_l - 0.02f, 0.0f);
    vu_r = vu_r < vu_tr ? fminf(vu_r + 0.04f, vu_tr) : fmaxf(vu_r - 0.02f, 0.0f);
    if (vu_l > vu_lp) vu_lp = vu_l; else vu_lp = fmaxf(0.0f, vu_lp - 0.005f);
    if (vu_r > vu_rp) vu_rp = vu_r; else vu_rp = fmaxf(0.0f, vu_rp - 0.005f);

    int vu_h = SCREEN_H - 70, vu_w = 60, vu_top = 20;
    float vals[2]  = {vu_l, vu_r};
    float peaks[2] = {vu_lp, vu_rp};
    const char* labels[2] = {"L", "R"};
    int xs[2] = {SCREEN_W/2 - 90, SCREEN_W/2 + 30};

    for (int s = 0; s < 2; s++) {
        fill_rect(xs[s], vu_top, vu_w, vu_h, rgb(0, 17, 34));
        int segs = 20;
        for (int i = 0; i < segs; i++) {
            float ratio = (float)i / segs;
            int filled = ratio < vals[s];
            unsigned short col;
            if (ratio > 0.85f)      col = filled ? rgb(255,34,0)  : rgb(34,5,0);
            else if (ratio > 0.65f) col = filled ? rgb(255,170,0) : rgb(34,21,0);
            else                    col = filled ? rgb(0,204,85)  : rgb(0,34,16);
            int sy = vu_top + vu_h - (i+1)*(vu_h/segs) + 1;
            fill_rect(xs[s]+4, sy, vu_w-8, vu_h/segs-2, col);
        }
        int peak_y = vu_top + vu_h - (int)(peaks[s] * vu_h);
        fill_rect(xs[s]+4, peak_y, vu_w-8, 2, rgb(255,255,255));
        draw_text(xs[s] + vu_w/2 - 3, vu_top + vu_h + 6, labels[s], rgb(0,170,255), 1);
        char dbuf[8];
        int db = (int)(vals[s] * 40) - 40;
        snprintf(dbuf, sizeof(dbuf), "%dDB", db);
        draw_text(xs[s] + vu_w/2 - 10, vu_top + vu_h + 16, dbuf, rgb(255,204,0), 1);
    }
    draw_track_info();
}

// ============================================================
// MODE 4: Dot matrix
// ============================================================
static void draw_mode4(void) {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, rgb(0, 13, 26));
    int cols = 40, rows = 16;
    int cw = SCREEN_W / cols, ch = (SCREEN_H - 50) / rows;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int bi = c * NUM_BARS / cols;
            float thr = (float)(rows - r) / rows * 120.0f;
            if (bars[bi] > thr) {
                float rat = bars[bi] / 120.0f;
                unsigned short col;
                if (rat > 0.85f)      col = rgb(255, 51, 0);
                else if (rat > 0.60f) col = rgb(255, 187, 0);
                else                  col = rgb(0, 153, 255);
                fill_rect(c*cw+1, r*ch+1, cw-2, ch-2, col);
            } else {
                fill_rect(c*cw+1, r*ch+1, cw-2, ch-2, rgb(0, 24, 51));
            }
        }
    }
    draw_track_info();
}

// ============================================================
// MODE 5: Starfield
// ============================================================
typedef struct { float x, y, z, speed; } Star;
static Star stars[120];
static int stars_init = 0;

static void draw_mode5(void) {
    if (!stars_init) {
        for (int i = 0; i < 120; i++) {
            stars[i].x = ((float)rand()/RAND_MAX - 0.5f) * 2.0f;
            stars[i].y = ((float)rand()/RAND_MAX - 0.5f) * 2.0f;
            stars[i].z = (float)rand()/RAND_MAX;
            stars[i].speed = 0.003f + ((float)rand()/RAND_MAX) * 0.007f;
        }
        stars_init = 1;
    }

    fill_rect(0, 0, SCREEN_W, SCREEN_H, rgb(0, 8, 20));

    float avg = 0;
    for (int i = 0; i < NUM_BARS; i++) avg += bars[i];
    avg /= (NUM_BARS * 120.0f);

    int cx = SCREEN_W / 2, cy = (SCREEN_H - 50) / 2;
    for (int i = 0; i < 120; i++) {
        stars[i].z += stars[i].speed * (1.0f + avg * 3.0f);
        if (stars[i].z > 1.0f) {
            stars[i].z = 0.01f;
            stars[i].x = ((float)rand()/RAND_MAX - 0.5f) * 2.0f;
            stars[i].y = ((float)rand()/RAND_MAX - 0.5f) * 2.0f;
        }
        int sx = (int)(cx + stars[i].x / stars[i].z * cx * 0.9f);
        int sy = (int)(cy + stars[i].y / stars[i].z * cy * 0.9f);
        if (sx < 0 || sx >= SCREEN_W || sy < 0 || sy >= SCREEN_H - 50) continue;
        int b = (int)(stars[i].z * 255);
        int sz = (int)(stars[i].z * 3) + 1;
        fill_rect(sx, sy, sz, sz, rgb(fminf(255,b+50), b, fminf(255,b+100)));
    }
    draw_track_info();
}

// ============================================================
// MODE 6: Sony MDX style
// ============================================================
static int scroll_x = SCREEN_W;

static void draw_mode6(void) {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, rgb(0, 5, 16));
    fill_rect(8, 8, SCREEN_W-16, SCREEN_H-55, rgb(0, 8, 32));

    // pixel grid bg
    for (int gx = 12; gx < SCREEN_W-16; gx += 9)
        for (int gy = 12; gy < SCREEN_H-57; gy += 9)
            fill_rect(gx, gy, 8, 8, rgb(0, 13, 40));

    // scrolling track name
    draw_text(scroll_x, SCREEN_H/2 - 16, track_name, rgb(232, 244, 255), 2);
    draw_text(scroll_x + 80, SCREEN_H/2 + 2, artist_name, rgb(255, 153, 0), 1);
    scroll_x -= 2;
    if (scroll_x < -(int)(strlen(track_name) * 12 + 20)) scroll_x = SCREEN_W;

    // AUX tag
    fill_rect(14, 14, 38, 18, rgb(255, 102, 0));
    draw_text(16, 18, "AUX", rgb(0, 0, 0), 1);
    fill_rect(58, 14, 28, 18, rgb(0, 136, 255));
    draw_text(60, 18, "MP3", rgb(255, 255, 255), 1);

    // mini EQ bottom
    int n_mini = 36, bw2 = 10, gap2 = 2;
    int total2 = n_mini * (bw2 + gap2) - gap2;
    int ex = (SCREEN_W - total2) / 2;
    int eq_y = SCREEN_H - 44;
    for (int i = 0; i < n_mini; i++) {
        int bi = i * NUM_BARS / n_mini;
        int bh = (int)(bars[bi] / 120.0f * 28.0f);
        for (int s = 0; s < bh; s++) {
            unsigned short col;
            if (s > 22)      col = rgb(255, 51, 0);
            else if (s > 14) col = rgb(255, 204, 0);
            else             col = rgb(0, 170, 255);
            fill_rect(ex + i*(bw2+gap2), eq_y + (28 - s*3) - 3, bw2, 2, col);
        }
    }
    draw_text(SCREEN_W - 40, SCREEN_H - 6, "SONY", rgb(30, 40, 60), 1);
}

// ============================================================
// MODE 7: Kenwood DPX-440 VFD SPEC display
// ============================================================
static void draw_mode7(void) {
    unsigned short bg   = rgb(8, 4, 0);
    unsigned short C    = rgb(255, 170, 0);   // amber
    unsigned short CL   = rgb(255, 204, 68);  // bright amber
    unsigned short CD   = rgb(80, 50, 0);     // dim amber
    unsigned short CRED = rgb(255, 68, 0);    // red peak

    fill_rect(0, 0, SCREEN_W, SCREEN_H, bg);
    // border
    draw_line(4, 4, SCREEN_W-4, 4, rgb(58, 40, 0));
    draw_line(4, SCREEN_H-4, SCREEN_W-4, SCREEN_H-4, rgb(58, 40, 0));
    draw_line(4, 4, 4, SCREEN_H-4, rgb(58, 40, 0));
    draw_line(SCREEN_W-4, 4, SCREEN_W-4, SCREEN_H-4, rgb(58, 40, 0));

    // top tags: CD TAPE TUNER
    const char* src_tags[] = {"CD", "TAPE", "TUNER"};
    for (int i = 0; i < 3; i++) {
        unsigned short col = (i == 0) ? CL : CD;
        fill_rect(14 + i*52, 10, (int)strlen(src_tags[i])*6+8, 14, bg);
        draw_line(14+i*52, 10, 14+i*52+(int)strlen(src_tags[i])*6+8, 10, col);
        draw_line(14+i*52, 24, 14+i*52+(int)strlen(src_tags[i])*6+8, 24, col);
        draw_line(14+i*52, 10, 14+i*52, 24, col);
        draw_line(14+i*52+(int)strlen(src_tags[i])*6+8, 10, 14+i*52+(int)strlen(src_tags[i])*6+8, 24, col);
        draw_text(14+i*52+4, 13, src_tags[i], col, 1);
    }

    // SPEC EQ tags
    const char* disp_tags[] = {"SPEC", "EQ"};
    for (int i = 0; i < 2; i++) {
        int tx = SCREEN_W/2 - 20 + i*48;
        draw_text(tx+4, 13, disp_tags[i], CL, 1);
        draw_line(tx, 10, tx+strlen(disp_tags[i])*6+8, 10, C);
        draw_line(tx, 24, tx+strlen(disp_tags[i])*6+8, 24, C);
        draw_line(tx, 10, tx, 24, C);
        draw_line(tx+strlen(disp_tags[i])*6+8, 10, tx+strlen(disp_tags[i])*6+8, 24, C);
    }
    draw_text(SCREEN_W - 56, 13, "KENWOOD", C, 1);

    // TR + track num
    draw_text(14, 34, "TR", CD, 1);
    char trbuf[4]; snprintf(trbuf, sizeof(trbuf), "%02d", track_num);
    draw_text(32, 30, trbuf, CL, 2);

    // time
    int mins = track_seconds / 60, secs = track_seconds % 60;
    char timebuf[8]; snprintf(timebuf, sizeof(timebuf), "%02d:%02d", mins, secs);
    draw_text(SCREEN_W - 74, 28, timebuf, CL, 2);
    draw_text(SCREEN_W - 12, 30, "M", CD, 1);
    draw_text(SCREEN_W - 12, 38, "S", CD, 1);

    // KENWOOD DPX-440
    draw_text(SCREEN_W - 130, 54, "KENWOOD DPX-440", C, 1);
    draw_text(SCREEN_W - 12, 54, "C", CD, 1);

    // Main EQ bars with horizontal stripe texture
    int eq_top = 62, eq_bot = 168, eq_h = eq_bot - eq_top;
    int bar_w = 14, bar_gap = 2;
    int total_w = NUM_BARS * (bar_w + bar_gap) - bar_gap;
    int eq_sx = (SCREEN_W - total_w) / 2;

    for (int i = 0; i < NUM_BARS; i++) {
        int x = eq_sx + i * (bar_w + bar_gap);
        int filled_h = (int)(bars[i] / 120.0f * eq_h);
        int by = eq_bot - filled_h;

        // horizontal stripes inside bar
        for (int sy = by; sy < eq_bot; sy += 3) {
            float row_ratio = (float)(eq_bot - sy) / eq_h;
            unsigned short col;
            if (row_ratio > 0.85f)      col = CRED;
            else if (row_ratio > 0.65f) col = C;
            else                        col = CL;
            fill_rect(x, sy, bar_w, 2, col);
        }

        // peak marker
        if (bar_peaks[i] > 4) {
            int py = eq_bot - (int)(bar_peaks[i] / 120.0f * eq_h) - 2;
            fill_rect(x, py, bar_w, 2, CL);
        }
    }

    // separator line
    draw_line(10, eq_bot + 2, SCREEN_W - 10, eq_bot + 2, C);

    // Bottom 7-band EQ with freq labels
    const char* freqs[] = {"63","160","400","1K","2.5K","6.3K","16K"};
    int n_freq = 7;
    int eq_low_top = eq_bot + 8, eq_low_h = 36;
    int pan_w = (SCREEN_W - 20) / n_freq;

    draw_text(12, eq_low_top + eq_low_h/2 - 4, "L", CD, 1);

    for (int i = 0; i < n_freq; i++) {
        int px = 20 + i * pan_w;
        // box
        draw_line(px, eq_low_top, px+pan_w-2, eq_low_top, CD);
        draw_line(px, eq_low_top+eq_low_h, px+pan_w-2, eq_low_top+eq_low_h, CD);
        draw_line(px, eq_low_top, px, eq_low_top+eq_low_h, CD);
        draw_line(px+pan_w-2, eq_low_top, px+pan_w-2, eq_low_top+eq_low_h, CD);

        int bi = i * NUM_BARS / n_freq;
        float energy = bars[bi] / 120.0f;
        int line_len = (int)((pan_w - 16) * fmaxf(0.1f, energy));
        fill_rect(px + 6, eq_low_top + eq_low_h/2 - 1, line_len, 3, C);

        // freq label
        draw_text(px + pan_w/2 - (int)strlen(freqs[i])*3, eq_low_top + eq_low_h + 4, freqs[i], CD, 1);
    }

    // Status line: LOUD DSP RPT RDM ST
    const char* status[] = {"LOUD","DSP","RPT","RDM","ST"};
    for (int i = 0; i < 5; i++)
        draw_text(14 + i*42, SCREEN_H - 20, status[i], i < 2 ? CL : C, 1);

    // VOL
    draw_text(SCREEN_W - 28, SCREEN_H - 22, "VOL", CD, 1);
    draw_text(SCREEN_W - 20, SCREEN_H - 12, "22", CL, 1);

    // scanlines
    for (int y = 0; y < SCREEN_H; y += 2)
        for (int x = 0; x < SCREEN_W; x++)
            put_pixel(x, y, (vram[y*512+x] >> 1) & 0x7BEF);
}

// ============================================================
// Mode dispatch
// ============================================================
typedef void (*DrawFn)(void);
static DrawFn draw_fns[NUM_MODES] = {
    draw_mode0, draw_mode1, draw_mode2, draw_mode3,
    draw_mode4, draw_mode5, draw_mode6, draw_mode7
};

static const char* mode_names[NUM_MODES] = {
    "EQ BARS", "OSCILLOSCOPE", "SPECTRUM", "VU METERS",
    "DOT MATRIX", "STARFIELD", "SONY MDX", "KENWOOD VFD"
};

// ============================================================
// Input handling
// ============================================================
static void handle_input(void) {
    SceCtrlData pad;
    sceCtrlReadBufferPositive(&pad, 1);

    static unsigned int prev_buttons = 0;
    unsigned int pressed = pad.Buttons & ~prev_buttons;

    if (pressed & PSP_CTRL_LTRIGGER) {
        current_mode = (current_mode - 1 + NUM_MODES) % NUM_MODES;
    }
    if (pressed & PSP_CTRL_RTRIGGER) {
        current_mode = (current_mode + 1) % NUM_MODES;
    }
    if (pressed & PSP_CTRL_TRIANGLE) {
        track_num++;
        track_seconds = 0;
    }
    if (pressed & PSP_CTRL_CROSS) {
        if (track_num > 1) { track_num--; track_seconds = 0; }
    }

    prev_buttons = pad.Buttons;
}

// ============================================================
// Draw mode name overlay (top center, fades out)
// ============================================================
static int mode_label_timer = 0;

static void draw_mode_label(void) {
    if (mode_label_timer <= 0) return;
    mode_label_timer--;
    unsigned short col = rgb(0, 204, 255);
    int x = (SCREEN_W - (int)strlen(mode_names[current_mode]) * 6) / 2;
    draw_text(x, 4, mode_names[current_mode], col, 1);
}

// ============================================================
// Main
// ============================================================
int main(void) {
    setup_callbacks();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    // set framebuffer
    sceDisplaySetMode(0, SCREEN_W, SCREEN_H);
    sceDisplaySetFrameBuf((void*)vram, 512, PSP_DISPLAY_PIXEL_FORMAT_565, 1);

    // seed random
    srand(sceKernelGetSystemTimeLow());

    // init bars
    for (int i = 0; i < NUM_BARS; i++) {
        bars[i] = 0;
        bar_targets[i] = 20 + (float)rand()/RAND_MAX * 60.0f;
        bar_peaks[i] = 0;
    }

    int prev_mode = -1;
    mode_label_timer = 120;

    while (running) {
        handle_input();

        if (current_mode != prev_mode) {
            mode_label_timer = 90;
            prev_mode = current_mode;
        }

        update_bars();
        draw_fns[current_mode]();
        draw_mode_label();

        // simulate track time
        if ((frame_tick % 60) == 0) track_seconds++;

        frame_tick++;
        sceDisplayWaitVblankStart();
    }

    sceKernelExitGame();
    return 0;
}
