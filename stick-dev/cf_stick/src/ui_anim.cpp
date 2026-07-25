#include "ui_anim.h"
#include <M5Unified.h>
#include <AnimatedGIF.h>
#include "assets/gif_idle.h"
#include "assets/gif_shake.h"

static AnimatedGIF s_gif;
static bool s_is_shake = false;       /* currently playing the shake animation */
static uint32_t s_next_frame_ms = 0;
static uint16_t s_line[240];

/* AnimatedGIF line callback: push one line of pixels to the display
 * (240x135 full screen, no offset). */
static void gif_draw(GIFDRAW *pDraw) {
    uint8_t *s = pDraw->pPixels;
    uint16_t *pal = (uint16_t *)pDraw->pPalette;
    for (int x = 0; x < pDraw->iWidth; x++) s_line[x] = pal[s[x]];
    M5.Display.pushImage(pDraw->iX, pDraw->iY + pDraw->y, pDraw->iWidth, 1, s_line);
}

static void open_gif(const uint8_t *data, uint32_t len) {
    s_gif.close();
    s_gif.open((uint8_t *)data, len, gif_draw);
    s_next_frame_ms = 0;
}

void ui_init(void) {
    M5.Display.setSwapBytes(false);   /* palette is RGB565_BE, no swap needed */
    s_gif.begin(GIF_PALETTE_RGB565_BE);
    ui_play_idle();
}

void ui_play_idle(void)  { s_is_shake = false; open_gif(gif_idle, gif_idle_len); }
void ui_play_shake(void) { s_is_shake = true;  open_gif(gif_shake, gif_shake_len); }
bool ui_shake_playing(void) { return s_is_shake; }

void ui_tick(void) {
    uint32_t now = millis();
    if (now < s_next_frame_ms) return;
    int delay_ms = 0;
    int more = s_gif.playFrame(false, &delay_ms);
    s_next_frame_ms = now + (delay_ms > 0 ? delay_ms : 50);
    if (!more) {                       /* finished one pass */
        if (s_is_shake) { ui_play_idle(); }   /* shake animation plays once */
        else { s_gif.reset(); }               /* idle loops forever */
    }
    /* battery overlay, redrawn on top of the GIF every frame */
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_GOLD, TFT_BLACK);
    M5.Display.setCursor(206, 4);
    M5.Display.printf("%3d%%", M5.Power.getBatteryLevel());
}
