/**
 * @file scene_standby.c
 * @brief Boot splash (opening_1) + standby loop (opening_2~6 shuffled).
 *
 * Timers are owned by the scene: the callback NULLs its own handle before
 * triggering a transition, and a DELETE event on the scene container kills
 * any timer still alive when the flow cleans the root.
 */

#include "tal_api.h"
#include "fortune_flow.h"

/***********************************************************
 ************************* boot ****************************
 ***********************************************************/
static lv_timer_t *s_boot_timer = NULL;

static void boot_done_cb(lv_timer_t *timer)
{
    lv_timer_del(timer);
    s_boot_timer = NULL;
    fortune_flow_goto(SCENE_STANDBY);
}

static void boot_delete_cb(lv_event_t *e)
{
    (void)e;
    if (s_boot_timer != NULL) {
        lv_timer_del(s_boot_timer);
        s_boot_timer = NULL;
    }
}

void scene_boot_enter(lv_obj_t *root)
{
    lv_obj_t *cont = lv_obj_create(root);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
    lv_obj_add_event_cb(cont, boot_delete_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *gif = lv_gif_create(cont);
    lv_gif_set_src(gif, &gif_opening_1);
    lv_obj_center(gif);

    s_boot_timer = lv_timer_create(boot_done_cb, 3000, NULL);
}

/***********************************************************
 ************************ standby **************************
 ***********************************************************/
static const lv_img_dsc_t *const STANDBY_GIFS[] = {
    &gif_opening_2, &gif_opening_3, &gif_opening_4, &gif_opening_5, &gif_opening_6,
};
#define STANDBY_GIF_COUNT ((int)(sizeof(STANDBY_GIFS) / sizeof(STANDBY_GIFS[0])))

static lv_timer_t *s_standby_timer = NULL;
static lv_obj_t *s_standby_gif = NULL;
static int s_order[STANDBY_GIF_COUNT];
static int s_order_idx = 0;

static void shuffle_order(void)
{
    for (int i = 0; i < STANDBY_GIF_COUNT; i++) {
        s_order[i] = i;
    }
    for (int i = STANDBY_GIF_COUNT - 1; i > 0; i--) {
        int j = tal_system_get_random(i + 1);
        int tmp = s_order[i];
        s_order[i] = s_order[j];
        s_order[j] = tmp;
    }
    s_order_idx = 0;
}

static void standby_next_cb(lv_timer_t *timer)
{
    (void)timer;
    s_order_idx++;
    if (s_order_idx >= STANDBY_GIF_COUNT) {
        shuffle_order();
    }
    lv_gif_set_src(s_standby_gif, STANDBY_GIFS[s_order[s_order_idx]]);
}

static void standby_delete_cb(lv_event_t *e)
{
    (void)e;
    if (s_standby_timer != NULL) {
        lv_timer_del(s_standby_timer);
        s_standby_timer = NULL;
    }
    s_standby_gif = NULL;
}

static void standby_click_cb(lv_event_t *e)
{
    (void)e;
    fortune_flow_goto(SCENE_CATEGORY);
}

void scene_standby_enter(lv_obj_t *root)
{
    lv_obj_t *cont = lv_obj_create(root);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cont, standby_delete_cb, LV_EVENT_DELETE, NULL);
    lv_obj_add_event_cb(cont, standby_click_cb, LV_EVENT_CLICKED, NULL);

    shuffle_order();
    s_standby_gif = lv_gif_create(cont);
    lv_gif_set_src(s_standby_gif, STANDBY_GIFS[s_order[0]]);
    lv_obj_center(s_standby_gif);

    /* tap hint pinned to the bottom, white plate so it reads on any frame */
    lv_obj_t *hint = lv_label_create(cont);
    lv_label_set_text(hint, "轻触屏幕 开始求签");
    lv_obj_set_style_text_font(hint, &font_px24, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(hint, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hint, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(hint, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(hint, 4, LV_PART_MAIN);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    s_standby_timer = lv_timer_create(standby_next_cb, 4000, NULL);
}
