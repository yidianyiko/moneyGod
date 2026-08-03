/**
 * @file scene_ask.c
 * @brief 解签 step 1: show the backend's yes/no question, take 是/否.
 *
 *        The question was prefetched when the result page appeared; this
 *        scene polls until it lands (12s cap), then reveals the buttons.
 *        Offline/timeout falls back to a built-in question so the flow
 *        never dead-ends.
 */

#include <string.h>

#include "tal_api.h"
#include "fortune_flow.h"
#include "fortune_net.h"

#define POLL_MS     200
#define ASK_WAIT_MS 12000

#define FALLBACK_ASK "你心里惦记的那件事，最近一周有新进展吗？"

static lv_timer_t *s_poll_timer = NULL;
static lv_obj_t *s_ask_label = NULL;
static lv_obj_t *s_btn_yes = NULL;
static lv_obj_t *s_btn_no = NULL;
static uint32_t s_waited_ms;

static void ask_delete_cb(lv_event_t *e)
{
    (void)e;
    if (s_poll_timer != NULL) {
        lv_timer_del(s_poll_timer);
        s_poll_timer = NULL;
    }
    s_ask_label = NULL;
    s_btn_yes = NULL;
    s_btn_no = NULL;
}

static void reply_click_cb(lv_event_t *e)
{
    fortune_extra_t *extra = fortune_flow_extra();
    extra->reply_yes = (int)(intptr_t)lv_event_get_user_data(e);

    /* fire the interpretation request; scene_interpret handles fallback */
    const char *cat_name = fortune_category_names[fortune_flow_category()];
    fortune_net_interpret_async(fortune_flow_result(), cat_name, extra->ask, extra->reply_yes);
    fortune_flow_goto(SCENE_INTERPRET);
}

/** Question landed (or fallback chosen): show it and reveal 是/否. */
static void reveal_question(const char *ask)
{
    fortune_extra_t *extra = fortune_flow_extra();
    if (extra->ask[0] == '\0') {
        strncpy(extra->ask, ask, sizeof(extra->ask) - 1);
        extra->ask[sizeof(extra->ask) - 1] = '\0';
    }
    if (s_ask_label != NULL) {
        lv_label_set_text(s_ask_label, extra->ask);
    }
    if (s_btn_yes != NULL) {
        lv_obj_clear_flag(s_btn_yes, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_btn_no != NULL) {
        lv_obj_clear_flag(s_btn_no, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_poll_timer != NULL) {
        lv_timer_del(s_poll_timer);
        s_poll_timer = NULL;
    }
}

static void ask_poll_cb(lv_timer_t *timer)
{
    (void)timer;
    s_waited_ms += POLL_MS;

    fortune_extra_t *extra = fortune_flow_extra();
    char buf[sizeof(extra->ask)];
    if (fortune_net_take_ask(buf, sizeof(buf)) == 0) {
        reveal_question(buf);
        return;
    }
    if (fortune_net_ask_poll() == FORTUNE_NET_FAILED || s_waited_ms >= ASK_WAIT_MS) {
        PR_NOTICE("[fortune-ask] question unavailable, local fallback");
        reveal_question(FALLBACK_ASK);
    }
}

void scene_ask_enter(lv_obj_t *root)
{
    fortune_extra_t *extra = fortune_flow_extra();

    lv_obj_t *cont = lv_obj_create(root);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
    lv_obj_add_event_cb(cont, ask_delete_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *header = lv_label_create(cont);
    lv_label_set_text(header, "财神有一问");
    lv_obj_set_style_text_font(header, &font_px36, LV_PART_MAIN);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 20);

    s_ask_label = lv_label_create(cont);
    lv_label_set_text(s_ask_label, "财神正在想题…");
    lv_obj_set_style_text_font(s_ask_label, &font_px24, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ask_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(s_ask_label, 8, LV_PART_MAIN);
    lv_obj_set_width(s_ask_label, FORTUNE_SCR_W - 64);
    lv_label_set_long_mode(s_ask_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_ask_label, LV_ALIGN_CENTER, 0, -16);

    s_btn_yes = lv_btn_create(cont);
    lv_obj_remove_style_all(s_btn_yes);
    fortune_style_btn(s_btn_yes);
    lv_obj_set_size(s_btn_yes, 180, 52);
    lv_obj_align(s_btn_yes, LV_ALIGN_BOTTOM_LEFT, 40, -16);
    lv_obj_add_event_cb(s_btn_yes, reply_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    lv_obj_t *yes_label = lv_label_create(s_btn_yes);
    lv_label_set_text(yes_label, "是");
    lv_obj_center(yes_label);

    s_btn_no = lv_btn_create(cont);
    lv_obj_remove_style_all(s_btn_no);
    fortune_style_btn(s_btn_no);
    lv_obj_set_size(s_btn_no, 180, 52);
    lv_obj_align(s_btn_no, LV_ALIGN_BOTTOM_RIGHT, -40, -16);
    lv_obj_add_event_cb(s_btn_no, reply_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)0);
    lv_obj_t *no_label = lv_label_create(s_btn_no);
    lv_label_set_text(no_label, "否");
    lv_obj_center(no_label);

    if (extra->ask[0] != '\0') {
        /* question already in hand (re-entry), show immediately */
        reveal_question(extra->ask);
        return;
    }

    /* buttons stay hidden until the question lands */
    lv_obj_add_flag(s_btn_yes, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_btn_no, LV_OBJ_FLAG_HIDDEN);
    s_waited_ms = 0;
    s_poll_timer = lv_timer_create(ask_poll_cb, POLL_MS, NULL);
}
