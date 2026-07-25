#include "ui_anim.h"
#include <M5Unified.h>
#include <AnimatedGIF.h>
#include "config.h"
#include "assets/gif_shake.h"

static AnimatedGIF s_gif;
static bool s_is_shake = false;       /* currently playing the shake animation */
static bool s_idle_drawn = false;     /* static idle frame already rendered */
static int  s_passes_left = 0;        /* remaining shake animation passes */
static uint32_t s_next_frame_ms = 0;
static uint32_t s_next_batt_ms = 0;
static uint16_t s_line[240];

/* AnimatedGIF line callback: push one line of pixels to the display
 * (240x135 full screen, no offset). Delta frames from the asset pipeline
 * mark unchanged pixels with a transparency index -- skip those so the
 * previous frame shows through instead of painting them black. */
static void gif_draw(GIFDRAW *pDraw) {
    uint8_t *s = pDraw->pPixels;
    uint16_t *pal = (uint16_t *)pDraw->pPalette;
    int y = pDraw->iY + pDraw->y;
    if (pDraw->ucHasTransparency) {
        uint8_t t = pDraw->ucTransparent;
        int x = 0;
        while (x < pDraw->iWidth) {
            while (x < pDraw->iWidth && s[x] == t) x++;   /* skip transparent run */
            int start = x;
            while (x < pDraw->iWidth && s[x] != t) {
                s_line[x - start] = pal[s[x]];
                x++;
            }
            if (x > start) {
                M5.Display.pushImage(pDraw->iX + start, y, x - start, 1, s_line);
            }
        }
    } else {
        for (int x = 0; x < pDraw->iWidth; x++) s_line[x] = pal[s[x]];
        M5.Display.pushImage(pDraw->iX, y, pDraw->iWidth, 1, s_line);
    }
}

static void open_gif(const uint8_t *data, uint32_t len) {
    s_gif.close();
    M5.Display.fillScreen(TFT_WHITE);   /* white base under partial frames */
    s_gif.open((uint8_t *)data, len, gif_draw);
    s_next_frame_ms = 0;
}

static void draw_battery(void) {
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_DARKGRAY, TFT_WHITE);
    M5.Display.setCursor(206, 4);
    M5.Display.printf("%3d%%", M5.Power.getBatteryLevel());
}

void ui_init(void) {
    M5.Display.setSwapBytes(false);   /* palette is RGB565_BE, no swap needed */
    s_gif.begin(GIF_PALETTE_RGB565_BE);
    ui_play_idle();
}

/* Idle is a static screen: first frame of the lottery GIF, rendered once.
 * Shaking plays the same GIF through, then returns to the static frame. */
void ui_play_idle(void)  { s_is_shake = false; s_idle_drawn = false; open_gif(gif_shake, gif_shake_len); }
void ui_play_shake(void) {
    s_is_shake = true;
    s_passes_left = SHAKE_ANIM_REPEATS;
    open_gif(gif_shake, gif_shake_len);
}
bool ui_shake_playing(void) { return s_is_shake; }

void ui_tick(void) {
    uint32_t now = millis();
    if (s_is_shake) {
        if (now < s_next_frame_ms) return;
        int delay_ms = 0;
        int more = s_gif.playFrame(false, &delay_ms);
        s_next_frame_ms = now + (delay_ms > 0 ? delay_ms : 50);
        if (!more) {                   /* finished one pass */
            if (--s_passes_left > 0) {
                s_gif.reset();         /* replay from the first frame */
            } else {
                ui_play_idle();        /* all passes done, back to idle */
                return;
            }
        }
        draw_battery();
    } else {
        if (!s_idle_drawn) {
            s_gif.playFrame(false, nullptr);   /* render first frame only */
            s_gif.close();
            s_idle_drawn = true;
            draw_battery();
            s_next_batt_ms = now + 5000;
        } else if (now >= s_next_batt_ms) {    /* refresh battery every 5 s */
            draw_battery();
            s_next_batt_ms = now + 5000;
        }
    }
}
