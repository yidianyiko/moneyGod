/**
 * @file fortune_flow.c
 * @brief Scene state machine: one full-screen root container, cleaned on
 *        every transition; scene modules build their widgets into it.
 *        An idle timer sends interactive scenes back to standby.
 */

#include <string.h>

#include "tal_api.h"
#include "fortune_flow.h"
#include "fortune_ble_remote.h"
#include "fortune_audio.h"

#define IDLE_TIMEOUT_MS (60 * 1000)
#define REMOTE_POLL_MS  100

/***********************************************************
 *********************** static state **********************
 ***********************************************************/
static lv_obj_t *s_root = NULL;
static fortune_scene_t s_scene = SCENE_BOOT;
static lv_timer_t *s_idle_timer = NULL;

/* shared draw context */
static fortune_category_t s_category = FORTUNE_CAT_TODAY;
static const char *s_question = "";
static const char *s_answer = "";
static fortune_result_t s_result;
static fortune_extra_t s_extra;

/***********************************************************
 ************************ idle timer ***********************
 ***********************************************************/
static void idle_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    /* boot/standby have no user to lose; draw must not be interrupted */
    if (s_scene == SCENE_CATEGORY || s_scene == SCENE_QUESTION || s_scene == SCENE_RESULT ||
        s_scene == SCENE_ASK || s_scene == SCENE_INTERPRET) {
        PR_NOTICE("[fortune-flow] idle timeout, back to standby");
        fortune_flow_goto(SCENE_STANDBY);
    }
}

/***********************************************************
 ********************* BLE shake remote ********************
 ***********************************************************/
/* Mixed-mode semantics (user confirmed): standby/result -> quick draw with
 * the generic "today" category; picking pages and the draw show ignore the
 * shake so a touch user in progress is never interrupted. */
static void remote_poll_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!fortune_ble_remote_take_event()) {
        return;
    }
    if (s_scene == SCENE_STANDBY || s_scene == SCENE_RESULT) {
        PR_NOTICE("[fortune-flow] shake remote: quick draw");
        fortune_flow_set_category(FORTUNE_CAT_TODAY);
        fortune_flow_set_question("摇签问天，近期运势如何", "心诚则灵，直说无妨");
        fortune_flow_goto(SCENE_DRAW);
    } else {
        PR_NOTICE("[fortune-flow] shake remote ignored in scene %d", s_scene);
    }
}

/***********************************************************
 ************************ public API ***********************
 ***********************************************************/
void fortune_flow_start(void)
{
    fortune_style_init();

    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    s_root = lv_obj_create(screen);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, FORTUNE_SCR_W, FORTUNE_SCR_H);
    lv_obj_center(s_root);
    fortune_style_page(s_root);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    s_idle_timer = lv_timer_create(idle_timer_cb, IDLE_TIMEOUT_MS, NULL);
    lv_timer_create(remote_poll_cb, REMOTE_POLL_MS, NULL);

    fortune_flow_goto(SCENE_BOOT);
}

void fortune_flow_goto(fortune_scene_t scene)
{
    if (s_root == NULL || scene >= SCENE_COUNT) {
        return;
    }

    lv_obj_clean(s_root); /* deletes child widgets and their timers' owners */
    s_scene = scene;
    if (s_idle_timer != NULL) {
        lv_timer_reset(s_idle_timer);
    }

    PR_DEBUG("[fortune-flow] enter scene %d", scene);
    switch (scene) {
    case SCENE_BOOT:
        scene_boot_enter(s_root);
        break;
    case SCENE_STANDBY:
        fortune_audio_quiet(); /* cut any leftover announcement */
        scene_standby_enter(s_root);
        break;
    case SCENE_CATEGORY:
        scene_category_enter(s_root);
        break;
    case SCENE_QUESTION:
        scene_question_enter(s_root);
        break;
    case SCENE_DRAW:
        fortune_audio_quiet();
        memset(&s_extra, 0, sizeof(s_extra)); /* fresh draw, fresh 解签 */
        scene_draw_enter(s_root);
        break;
    case SCENE_RESULT:
        scene_result_enter(s_root);
        break;
    case SCENE_ASK:
        scene_ask_enter(s_root);
        break;
    case SCENE_INTERPRET:
        scene_interpret_enter(s_root);
        break;
    default:
        break;
    }
}

/* --- shared draw context --- */
void fortune_flow_set_category(fortune_category_t cat)
{
    s_category = cat;
}

fortune_category_t fortune_flow_category(void)
{
    return s_category;
}

void fortune_flow_set_question(const char *question, const char *answer)
{
    s_question = question ? question : "";
    s_answer = answer ? answer : "";
}

const char *fortune_flow_question(void)
{
    return s_question;
}

const char *fortune_flow_answer(void)
{
    return s_answer;
}

fortune_result_t *fortune_flow_result(void)
{
    return &s_result;
}

fortune_extra_t *fortune_flow_extra(void)
{
    return &s_extra;
}
