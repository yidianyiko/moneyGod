/**
 * @file fortune_data.h
 * @brief Cyber Fortune Temple v2 — shared data types, fonts and GIF assets.
 */
#ifndef FORTUNE_DATA_H
#define FORTUNE_DATA_H

#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Landscape geometry (BOARD_LCD_ROTATION = 90). */
#define FORTUNE_SCR_W 480
#define FORTUNE_SCR_H 320

/* Fusion Pixel fonts (src/assets/font_px*.c) */
LV_FONT_DECLARE(font_px24); /* GB2312 level-1 full set, body text   */
LV_FONT_DECLARE(font_px36); /* narrow charset, titles / grade badge */

/* GIF assets (src/assets/gif_*.c, raw GIF bytes wrapped in lv_img_dsc_t) */
LV_IMG_DECLARE(gif_opening_1);
LV_IMG_DECLARE(gif_opening_2);
LV_IMG_DECLARE(gif_opening_3);
LV_IMG_DECLARE(gif_opening_4);
LV_IMG_DECLARE(gif_opening_5);
LV_IMG_DECLARE(gif_opening_6);
LV_IMG_DECLARE(gif_thinking);
LV_IMG_DECLARE(gif_getting_lottery);
LV_IMG_DECLARE(gif_lottery_get);

/* Fortune categories shown on the 6-grid page. */
typedef enum {
    FORTUNE_CAT_WEALTH = 0, /* 财运     */
    FORTUNE_CAT_CAREER,     /* 事业     */
    FORTUNE_CAT_LOVE,       /* 姻缘     */
    FORTUNE_CAT_STUDY,      /* 学业     */
    FORTUNE_CAT_HEALTH,     /* 健康     */
    FORTUNE_CAT_TODAY,      /* 今日运势 */
    FORTUNE_CAT_COUNT
} fortune_category_t;

extern const char *const fortune_category_names[FORTUNE_CAT_COUNT];

/* One drawn lot — filled either by the backend (fortune_net) or the local
 * fallback (fortune_fallback). All strings are UTF-8. */
typedef struct {
    int  lot_no;           /* 1..100                          */
    char grade[16];        /* 上上/上/中/下/下下              */
    int  grade_score;      /* 1..5                            */
    char poem[4][64];      /* four vernacular verse lines     */
    char explanation[512]; /* targeted explanation            */
    char advice[256];      /* action advice                   */
} fortune_result_t;

/* Interpretation add-on (解签互动): yes/no question + targeted reading.
 * Cleared on every new draw; empty strings mean "stage not reached". */
typedef struct {
    char ask[192];       /* yes/no question from the backend  */
    int  reply_yes;      /* 1 = 是, 0 = 否                    */
    char interpret[640]; /* targeted interpretation           */
} fortune_extra_t;

#ifdef __cplusplus
}
#endif

#endif /* FORTUNE_DATA_H */
