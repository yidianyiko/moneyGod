/**
 * @file scene_result.c
 * @brief Lot page: grade header, poem, explanation/advice (scrollable),
 *        plus [解签] [打印签文] [再求一签]. Entering this scene also
 *        prefetches the 解签 yes/no question in the background.
 */

#include <stdio.h>
#include <string.h>

#include "tal_api.h"
#include "fortune_flow.h"
#include "fortune_net.h"
#include "fortune_printer.h"
#include "fortune_audio.h"

#define HEADER_Y    14
#define BODY_Y      62
#define BODY_H      186
#define BTN_W       140
#define BTN_H       48

static lv_obj_t *s_print_label = NULL;

static void print_click_cb(lv_event_t *e)
{
    (void)e;
    if (fortune_printer_busy()) {
        return;
    }
    fortune_printer_print(fortune_flow_result());
    if (s_print_label != NULL) {
        lv_label_set_text(s_print_label, "已送打印");
    }
}

static void again_click_cb(lv_event_t *e)
{
    (void)e;
    /* back to the standby loop; the next draw starts from a screen tap */
    fortune_flow_goto(SCENE_STANDBY);
}

static void interpret_click_cb(lv_event_t *e)
{
    (void)e;
    fortune_flow_goto(SCENE_ASK);
}

static void result_delete_cb(lv_event_t *e)
{
    (void)e;
    s_print_label = NULL;
}

void scene_result_enter(lv_obj_t *root)
{
    const fortune_result_t *res = fortune_flow_result();
    char buf[600];

    lv_obj_t *cont = lv_obj_create(root);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
    lv_obj_add_event_cb(cont, result_delete_cb, LV_EVENT_DELETE, NULL);

    /* header: lot number + grade (px36 charset covers 0-9 · 第签 grades) */
    lv_obj_t *header = lv_label_create(cont);
    snprintf(buf, sizeof(buf), "第 %d 签 · %s", res->lot_no, res->grade);
    lv_label_set_text(header, buf);
    lv_obj_set_style_text_font(header, &font_px36, LV_PART_MAIN);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, HEADER_Y);

    /* scrollable body: poem + divider + explanation + advice */
    lv_obj_t *body = lv_obj_create(cont);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, FORTUNE_SCR_W - 32, BODY_H);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, BODY_Y);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(body, 8, LV_PART_MAIN);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);

    lv_obj_t *poem = lv_label_create(body);
    snprintf(buf, sizeof(buf), "%s\n%s\n%s\n%s", res->poem[0], res->poem[1], res->poem[2], res->poem[3]);
    lv_label_set_text(poem, buf);
    lv_obj_set_style_text_font(poem, &font_px24, LV_PART_MAIN);
    lv_obj_set_style_text_align(poem, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(poem, 6, LV_PART_MAIN);

    lv_obj_t *divider = lv_label_create(body);
    lv_label_set_text(divider, "- - - - - - - - - - - -");
    lv_obj_set_style_text_font(divider, &font_px24, LV_PART_MAIN);

    lv_obj_t *expl = lv_label_create(body);
    snprintf(buf, sizeof(buf), "解签：%s", res->explanation);
    lv_label_set_text(expl, buf);
    lv_obj_set_style_text_font(expl, &font_px24, LV_PART_MAIN);
    lv_obj_set_width(expl, FORTUNE_SCR_W - 48);
    lv_label_set_long_mode(expl, LV_LABEL_LONG_WRAP);

    lv_obj_t *advice = lv_label_create(body);
    snprintf(buf, sizeof(buf), "指点：%s", res->advice);
    lv_label_set_text(advice, buf);
    lv_obj_set_style_text_font(advice, &font_px24, LV_PART_MAIN);
    lv_obj_set_width(advice, FORTUNE_SCR_W - 48);
    lv_label_set_long_mode(advice, LV_LABEL_LONG_WRAP);

    /* bottom actions: [解签] [打印] [再求一签] */
    lv_obj_t *btn_itp = lv_btn_create(cont);
    lv_obj_remove_style_all(btn_itp);
    fortune_style_btn(btn_itp);
    lv_obj_set_size(btn_itp, BTN_W, BTN_H);
    lv_obj_align(btn_itp, LV_ALIGN_BOTTOM_LEFT, 16, -12);
    lv_obj_add_event_cb(btn_itp, interpret_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *itp_label = lv_label_create(btn_itp);
    lv_label_set_text(itp_label, "解签");
    lv_obj_center(itp_label);

    lv_obj_t *btn_print = lv_btn_create(cont);
    lv_obj_remove_style_all(btn_print);
    fortune_style_btn(btn_print);
    lv_obj_set_size(btn_print, BTN_W, BTN_H);
    lv_obj_align(btn_print, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_event_cb(btn_print, print_click_cb, LV_EVENT_CLICKED, NULL);
    s_print_label = lv_label_create(btn_print);
    lv_label_set_text(s_print_label, "打印签文");
    lv_obj_center(s_print_label);

    lv_obj_t *btn_again = lv_btn_create(cont);
    lv_obj_remove_style_all(btn_again);
    fortune_style_btn(btn_again);
    lv_obj_set_size(btn_again, BTN_W, BTN_H);
    lv_obj_align(btn_again, LV_ALIGN_BOTTOM_RIGHT, -16, -12);
    lv_obj_add_event_cb(btn_again, again_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *again_label = lv_label_create(btn_again);
    lv_label_set_text(again_label, "再求一签");
    lv_obj_center(again_label);

    /* prefetch the 解签 question so tapping [解签] feels instant */
    fortune_extra_t *extra = fortune_flow_extra();
    if (extra->ask[0] == '\0' && fortune_net_is_up()) {
        const char *cat_name = fortune_category_names[fortune_flow_category()];
        if (fortune_net_ask_async(res, cat_name) == 0) {
            PR_NOTICE("[fortune-result] ask prefetch fired");
        }
    }

    /* coin jingle + 奶气萌娃 announcement (only on the first reveal) */
    if (extra->ask[0] == '\0' && extra->interpret[0] == '\0') {
        fortune_audio_fx(FORTUNE_FX_COIN);
        snprintf(buf, sizeof(buf), "恭喜！第%d签，%s签！%s，%s，%s，%s……汪汪！",
                 res->lot_no, res->grade, res->poem[0], res->poem[1], res->poem[2], res->poem[3]);
        fortune_audio_announce(buf);

        /* auto-print the ticket on the first reveal (no button tap needed) */
        if (!fortune_printer_busy()) {
            fortune_printer_print(res);
            if (s_print_label != NULL) {
                lv_label_set_text(s_print_label, "已送打印");
            }
        }
    }
}
