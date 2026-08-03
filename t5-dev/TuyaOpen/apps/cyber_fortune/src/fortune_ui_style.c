/**
 * @file fortune_ui_style.c
 * @brief Shared pixel-art look: white background, black pixels, thick
 *        borders, zero radius. Buttons invert when pressed.
 */

#include "fortune_flow.h"
#include "fortune_audio.h"

static lv_style_t s_st_page;
static lv_style_t s_st_btn;
static lv_style_t s_st_btn_pressed;
static int s_inited = 0;

/* every styled button gets the same tap blip */
static void btn_fx_cb(lv_event_t *e)
{
    (void)e;
    fortune_audio_fx(FORTUNE_FX_DING);
}

void fortune_style_init(void)
{
    if (s_inited) {
        return;
    }
    s_inited = 1;

    lv_style_init(&s_st_page);
    lv_style_set_bg_color(&s_st_page, lv_color_white());
    lv_style_set_bg_opa(&s_st_page, LV_OPA_COVER);
    lv_style_set_text_color(&s_st_page, lv_color_black());
    lv_style_set_text_font(&s_st_page, &font_px24);
    lv_style_set_border_width(&s_st_page, 0);
    lv_style_set_radius(&s_st_page, 0);
    lv_style_set_pad_all(&s_st_page, 0);

    lv_style_init(&s_st_btn);
    lv_style_set_bg_color(&s_st_btn, lv_color_white());
    lv_style_set_bg_opa(&s_st_btn, LV_OPA_COVER);
    lv_style_set_text_color(&s_st_btn, lv_color_black());
    lv_style_set_text_font(&s_st_btn, &font_px24);
    lv_style_set_border_color(&s_st_btn, lv_color_black());
    lv_style_set_border_width(&s_st_btn, 3);
    lv_style_set_radius(&s_st_btn, 0);
    lv_style_set_shadow_width(&s_st_btn, 0);

    lv_style_init(&s_st_btn_pressed);
    lv_style_set_bg_color(&s_st_btn_pressed, lv_color_black());
    lv_style_set_text_color(&s_st_btn_pressed, lv_color_white());
}

void fortune_style_page(lv_obj_t *obj)
{
    lv_obj_add_style(obj, &s_st_page, LV_PART_MAIN);
}

void fortune_style_btn(lv_obj_t *btn)
{
    lv_obj_add_style(btn, &s_st_btn, LV_PART_MAIN);
    lv_obj_add_style(btn, &s_st_btn_pressed, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, btn_fx_cb, LV_EVENT_PRESSED, NULL);
}
