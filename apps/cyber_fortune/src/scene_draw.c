/**
 * @file scene_draw.c
 * @brief Draw choreography, driven by a 200ms tick against known GIF
 *        durations (lv_gif has no loop-complete callback):
 *
 *        thinking (4.0s/loop) — min one loop, poll the backend, 12s cap
 *          -> getting_lottery (1.2s/loop, played 2 loops = 2.4s)
 *          -> lottery_get (1.8s once, then pause + 0.5s hold)
 *          -> SCENE_RESULT
 *
 *        The request is fired on entry; if WiFi is down, the request fails
 *        or the cap is hit, the local fallback fills the result and the
 *        show goes on — the user never sees a difference.
 */

#include "tal_api.h"
#include "fortune_flow.h"
#include "fortune_net.h"
#include "fortune_fallback.h"
#include "fortune_audio.h"

#define TICK_MS          200
#define THINKING_MIN_MS  4000  /* one full thinking loop     */
#define THINKING_MAX_MS  12000 /* hard cap, then fallback    */
#define GETTING_MS       2400  /* two getting_lottery loops  */
#define LOTTERY_MS       1800  /* lottery_get single pass    */
#define HOLD_MS          500   /* freeze on the last frame   */

typedef enum {
    PHASE_THINKING = 0,
    PHASE_GETTING,
    PHASE_LOTTERY,
    PHASE_HOLD,
} draw_phase_t;

static lv_timer_t *s_tick_timer = NULL;
static lv_obj_t *s_gif = NULL;
static draw_phase_t s_phase;
static uint32_t s_elapsed_ms;
static int s_local_only; /* no request in flight, fallback directly */

static void draw_delete_cb(lv_event_t *e)
{
    (void)e;
    if (s_tick_timer != NULL) {
        lv_timer_del(s_tick_timer);
        s_tick_timer = NULL;
    }
    s_gif = NULL;
}

static void enter_phase(draw_phase_t phase)
{
    s_phase = phase;
    s_elapsed_ms = 0;
    switch (phase) {
    case PHASE_GETTING:
        lv_gif_set_src(s_gif, &gif_getting_lottery);
        fortune_audio_fx(FORTUNE_FX_SHAKE); /* 签筒摇动沙沙声 */
        break;
    case PHASE_LOTTERY:
        lv_gif_set_src(s_gif, &gif_lottery_get);
        break;
    case PHASE_HOLD:
        lv_gif_pause(s_gif);
        break;
    default:
        break;
    }
}

/** Thinking phase gate: true once the result (net or fallback) is in. */
static int thinking_resolve(void)
{
    if (s_local_only) {
        fortune_fallback_get(fortune_flow_category(), fortune_flow_result());
        return 1;
    }

    fortune_net_state_t st = fortune_net_poll();
    if (st == FORTUNE_NET_DONE) {
        if (fortune_net_take_result(fortune_flow_result()) == 0) {
            return 1;
        }
        st = FORTUNE_NET_FAILED; /* should not happen, treat as failure */
    }
    if (st == FORTUNE_NET_FAILED || s_elapsed_ms >= THINKING_MAX_MS) {
        PR_NOTICE("[fortune-draw] backend unavailable (%d), local fallback", st);
        fortune_fallback_get(fortune_flow_category(), fortune_flow_result());
        return 1;
    }
    return 0; /* keep thinking */
}

static void draw_tick_cb(lv_timer_t *timer)
{
    s_elapsed_ms += TICK_MS;

    switch (s_phase) {
    case PHASE_THINKING:
        if (s_elapsed_ms >= THINKING_MIN_MS && thinking_resolve()) {
            enter_phase(PHASE_GETTING);
        }
        break;
    case PHASE_GETTING:
        if (s_elapsed_ms >= GETTING_MS) {
            enter_phase(PHASE_LOTTERY);
        }
        break;
    case PHASE_LOTTERY:
        if (s_elapsed_ms >= LOTTERY_MS) {
            enter_phase(PHASE_HOLD);
        }
        break;
    case PHASE_HOLD:
        if (s_elapsed_ms >= HOLD_MS) {
            lv_timer_del(timer);
            s_tick_timer = NULL;
            fortune_flow_goto(SCENE_RESULT);
        }
        break;
    default:
        break;
    }
}

void scene_draw_enter(lv_obj_t *root)
{
    lv_obj_t *cont = lv_obj_create(root);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
    lv_obj_add_event_cb(cont, draw_delete_cb, LV_EVENT_DELETE, NULL);

    s_gif = lv_gif_create(cont);
    lv_gif_set_src(s_gif, &gif_thinking);
    lv_obj_center(s_gif);

    /* fire the backend request while the mascot "thinks" */
    s_local_only = 0;
    const char *cat_name = fortune_category_names[fortune_flow_category()];
    if (!fortune_net_is_up() ||
        fortune_net_draw_async(cat_name, fortune_flow_question(), fortune_flow_answer()) != 0) {
        PR_NOTICE("[fortune-draw] offline, will use local fallback");
        s_local_only = 1;
    }

    s_phase = PHASE_THINKING;
    s_elapsed_ms = 0;
    fortune_audio_fx(FORTUNE_FX_BARK); /* 奶狗汪汪，掐指一算开场 */
    s_tick_timer = lv_timer_create(draw_tick_cb, TICK_MS, NULL);
}
