/**
 * @file scene_question.c
 * @brief One follow-up question with 3-4 option buttons; the picked option
 *        goes to the backend so the fortune can speak to the user.
 */

#include <stdint.h>

#include "fortune_flow.h"
#include "fortune_question.h"

#define OPT_W     360
#define OPT_H     52
#define OPT_GAP   12

static const fortune_question_t *s_question = NULL;

static void option_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_question == NULL || idx < 0 || idx >= s_question->option_count) {
        return;
    }
    /* question bank strings are const data, pointers stay valid */
    fortune_flow_set_question(s_question->text, s_question->options[idx]);
    fortune_flow_goto(SCENE_DRAW);
}

void scene_question_enter(lv_obj_t *root)
{
    s_question = fortune_question_pick(fortune_flow_category());

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, s_question->text);
    lv_obj_set_style_text_font(title, &font_px24, LV_PART_MAIN);
    lv_obj_set_width(title, FORTUNE_SCR_W - 40);
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    int n = s_question->option_count;
    int block_h = n * OPT_H + (n - 1) * OPT_GAP;
    /* options centered in the space below the (possibly 2-line) question */
    int y0 = 80 + (FORTUNE_SCR_H - 80 - block_h) / 2;
    if (y0 < 80) {
        y0 = 80;
    }

    for (int i = 0; i < n; i++) {
        lv_obj_t *btn = lv_btn_create(root);
        lv_obj_remove_style_all(btn);
        fortune_style_btn(btn);
        lv_obj_set_size(btn, OPT_W, OPT_H);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, y0 + i * (OPT_H + OPT_GAP));
        lv_obj_add_event_cb(btn, option_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, s_question->options[i]);
        lv_obj_center(label);
    }
}
