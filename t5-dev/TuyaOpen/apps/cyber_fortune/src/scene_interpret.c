/**
 * @file scene_interpret.c
 * @brief 解签 step 2: thinking GIF while the targeted interpretation is
 *        fetched (15s cap, offline falls back to a built-in reading),
 *        then the text plus [打印] [再求一签].
 */

#include <stdio.h>
#include <string.h>

#include "tal_api.h"
#include "fortune_flow.h"
#include "fortune_net.h"
#include "fortune_printer.h"
#include "fortune_audio.h"

#define POLL_MS     200
#define ITP_WAIT_MS 15000

/* mirrors the backend's fallback tone so offline feels identical */
#define FALLBACK_ITP_YES                                                                           \
    "既然已经在动，这支签就是让你顺势而为：把节奏稳住，不贪快不冒进，"                             \
    "把已经开始的事做完整。财神看好你这股劲，汪。"
#define FALLBACK_ITP_NO                                                                            \
    "没动静也不用慌，这支签提醒你：变化往往在你收拾利索之后才来。"                                 \
    "先把手头最小的一件事收个尾，给新机会腾位置，汪。"

static lv_timer_t *s_poll_timer = NULL;
static lv_obj_t *s_cont = NULL;
static uint32_t s_waited_ms;
static lv_obj_t *s_print_label = NULL;

static void interpret_delete_cb(lv_event_t *e)
{
    (void)e;
    if (s_poll_timer != NULL) {
        lv_timer_del(s_poll_timer);
        s_poll_timer = NULL;
    }
    s_cont = NULL;
    s_print_label = NULL;
}

static void print_click_cb(lv_event_t *e)
{
    (void)e;
    if (fortune_printer_busy()) {
        return;
    }
    fortune_printer_print_full(fortune_flow_result(), fortune_flow_extra());
    if (s_print_label != NULL) {
        lv_label_set_text(s_print_label, "已送打印");
    }
}

static void again_click_cb(lv_event_t *e)
{
    (void)e;
    fortune_flow_goto(SCENE_STANDBY);
}

/** Interpretation in hand: swap the loading view for the reading. */
static void show_interpretation(void)
{
    const fortune_extra_t *extra = fortune_flow_extra();

    lv_obj_clean(s_cont);

    lv_obj_t *header = lv_label_create(s_cont);
    lv_label_set_text(header, "财神解签");
    lv_obj_set_style_text_font(header, &font_px36, LV_PART_MAIN);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 14);

    /* scrollable body: the question/answer recap + the reading */
    lv_obj_t *body = lv_obj_create(s_cont);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, FORTUNE_SCR_W - 32, 186);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(body, 8, LV_PART_MAIN);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);

    char buf[280];
    lv_obj_t *recap = lv_label_create(body);
    snprintf(buf, sizeof(buf), "问：%s\n答：%s", extra->ask, extra->reply_yes ? "是" : "否");
    lv_label_set_text(recap, buf);
    lv_obj_set_style_text_font(recap, &font_px24, LV_PART_MAIN);
    lv_obj_set_width(recap, FORTUNE_SCR_W - 48);
    lv_label_set_long_mode(recap, LV_LABEL_LONG_WRAP);

    lv_obj_t *divider = lv_label_create(body);
    lv_label_set_text(divider, "- - - - - - - - - - - -");
    lv_obj_set_style_text_font(divider, &font_px24, LV_PART_MAIN);

    lv_obj_t *reading = lv_label_create(body);
    lv_label_set_text(reading, extra->interpret);
    lv_obj_set_style_text_font(reading, &font_px24, LV_PART_MAIN);
    lv_obj_set_width(reading, FORTUNE_SCR_W - 48);
    lv_label_set_long_mode(reading, LV_LABEL_LONG_WRAP);

    /* bottom actions */
    lv_obj_t *btn_print = lv_btn_create(s_cont);
    lv_obj_remove_style_all(btn_print);
    fortune_style_btn(btn_print);
    lv_obj_set_size(btn_print, 210, 48);
    lv_obj_align(btn_print, LV_ALIGN_BOTTOM_LEFT, 16, -12);
    lv_obj_add_event_cb(btn_print, print_click_cb, LV_EVENT_CLICKED, NULL);
    s_print_label = lv_label_create(btn_print);
    lv_label_set_text(s_print_label, "打印签文");
    lv_obj_center(s_print_label);

    lv_obj_t *btn_again = lv_btn_create(s_cont);
    lv_obj_remove_style_all(btn_again);
    fortune_style_btn(btn_again);
    lv_obj_set_size(btn_again, 210, 48);
    lv_obj_align(btn_again, LV_ALIGN_BOTTOM_RIGHT, -16, -12);
    lv_obj_add_event_cb(btn_again, again_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *again_label = lv_label_create(btn_again);
    lv_label_set_text(again_label, "再求一签");
    lv_obj_center(again_label);
}

static void resolve_fallback(void)
{
    fortune_extra_t *extra = fortune_flow_extra();
    strncpy(extra->interpret, extra->reply_yes ? FALLBACK_ITP_YES : FALLBACK_ITP_NO,
            sizeof(extra->interpret) - 1);
    extra->interpret[sizeof(extra->interpret) - 1] = '\0';
}

static void interpret_poll_cb(lv_timer_t *timer)
{
    s_waited_ms += POLL_MS;

    fortune_extra_t *extra = fortune_flow_extra();
    int ready = 0;
    if (fortune_net_take_interpret(extra->interpret, sizeof(extra->interpret)) == 0) {
        ready = 1;
    } else if (fortune_net_interpret_poll() == FORTUNE_NET_FAILED || s_waited_ms >= ITP_WAIT_MS) {
        PR_NOTICE("[fortune-interpret] backend unavailable, local fallback");
        resolve_fallback();
        ready = 1;
    }

    if (ready) {
        lv_timer_del(timer);
        s_poll_timer = NULL;
        show_interpretation();
    }
}

void scene_interpret_enter(lv_obj_t *root)
{
    fortune_extra_t *extra = fortune_flow_extra();

    s_cont = lv_obj_create(root);
    lv_obj_remove_style_all(s_cont);
    lv_obj_set_size(s_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_add_event_cb(s_cont, interpret_delete_cb, LV_EVENT_DELETE, NULL);

    if (extra->interpret[0] != '\0') {
        show_interpretation();
        return;
    }

    /* loading: reuse the thinking mascot while the reading is generated */
    lv_obj_t *gif = lv_gif_create(s_cont);
    lv_gif_set_src(gif, &gif_thinking);
    lv_obj_center(gif);
    fortune_audio_fx(FORTUNE_FX_BARK); /* 奶狗汪汪，解签 loading 开场 */

    lv_obj_t *tip = lv_label_create(s_cont);
    lv_label_set_text(tip, "财神掐指一算…");
    lv_obj_set_style_text_font(tip, &font_px24, LV_PART_MAIN);
    lv_obj_align(tip, LV_ALIGN_BOTTOM_MID, 0, -18);

    /* offline and the request never went out: resolve on the first tick */
    if (!fortune_net_is_up() && fortune_net_interpret_poll() != FORTUNE_NET_BUSY) {
        resolve_fallback();
        show_interpretation();
        return;
    }

    s_waited_ms = 0;
    s_poll_timer = lv_timer_create(interpret_poll_cb, POLL_MS, NULL);
}
