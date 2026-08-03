/**
 * @file scene_category.c
 * @brief 3×2 category grid — pick what to ask the God of Wealth about.
 */

#include <stdint.h>

#include "fortune_flow.h"

#define GRID_COLS  3
#define BTN_W      140
#define BTN_H      74
#define BTN_GAP_X  12
#define BTN_GAP_Y  14
#define GRID_TOP   118

static void category_click_cb(lv_event_t *e)
{
    fortune_category_t cat = (fortune_category_t)(intptr_t)lv_event_get_user_data(e);
    fortune_flow_set_category(cat);
    fortune_flow_goto(SCENE_QUESTION);
}

void scene_category_enter(lv_obj_t *root)
{
    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "赛博财神庙");
    lv_obj_set_style_text_font(title, &font_px36, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t *subtitle = lv_label_create(root);
    lv_label_set_text(subtitle, "今日想问哪方面？");
    lv_obj_set_style_text_font(subtitle, &font_px24, LV_PART_MAIN);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 72);

    int grid_w = GRID_COLS * BTN_W + (GRID_COLS - 1) * BTN_GAP_X;
    int x0 = (FORTUNE_SCR_W - grid_w) / 2;

    for (int i = 0; i < FORTUNE_CAT_COUNT; i++) {
        int col = i % GRID_COLS;
        int row = i / GRID_COLS;

        lv_obj_t *btn = lv_btn_create(root);
        lv_obj_remove_style_all(btn);
        fortune_style_btn(btn);
        lv_obj_set_size(btn, BTN_W, BTN_H);
        lv_obj_set_pos(btn, x0 + col * (BTN_W + BTN_GAP_X), GRID_TOP + row * (BTN_H + BTN_GAP_Y));
        lv_obj_add_event_cb(btn, category_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, fortune_category_names[i]);
        lv_obj_center(label);
    }
}
