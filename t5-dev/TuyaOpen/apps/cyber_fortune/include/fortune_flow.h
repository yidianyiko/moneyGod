/**
 * @file fortune_flow.h
 * @brief Scene state machine for the Cyber Fortune Temple v2 UI.
 *
 * All scenes are built into a single full-screen root container that the
 * flow cleans on every transition. Scene modules implement enter/exit.
 */
#ifndef FORTUNE_FLOW_H
#define FORTUNE_FLOW_H

#include "fortune_data.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SCENE_BOOT = 0, /* opening_1 loop while WiFi/USB come up  */
    SCENE_STANDBY,  /* opening_2~6 shuffled rotation           */
    SCENE_CATEGORY, /* 6-grid category picker                  */
    SCENE_QUESTION, /* one follow-up question, 3-4 options     */
    SCENE_DRAW,     /* thinking -> getting_lottery -> lottery  */
    SCENE_RESULT,   /* lot page + [解签] [print] [again]        */
    SCENE_ASK,      /* yes/no question from the backend        */
    SCENE_INTERPRET,/* targeted interpretation + final actions */
    SCENE_COUNT
} fortune_scene_t;

/** Build root container + styles and enter SCENE_BOOT.
 *  Call once under lv_vendor_disp_lock(). */
void fortune_flow_start(void);

/** Switch scene (LVGL task context only). */
void fortune_flow_goto(fortune_scene_t scene);

/* --- shared draw context (owned by the flow) --- */
void fortune_flow_set_category(fortune_category_t cat);
fortune_category_t fortune_flow_category(void);
void fortune_flow_set_question(const char *question, const char *answer);
const char *fortune_flow_question(void);
const char *fortune_flow_answer(void);
fortune_result_t *fortune_flow_result(void); /* filled by scene_draw */
fortune_extra_t *fortune_flow_extra(void);   /* 解签 context, cleared per draw */

/* --- shared UI style helpers (fortune_ui_style.c) --- */
void fortune_style_init(void);
/** White page, black text, 24px font. */
void fortune_style_page(lv_obj_t *obj);
/** Pixel button: white bg, thick black border, inverts when pressed. */
void fortune_style_btn(lv_obj_t *btn);

/* --- scene modules --- */
void scene_boot_enter(lv_obj_t *root);
void scene_standby_enter(lv_obj_t *root);
void scene_category_enter(lv_obj_t *root);
void scene_question_enter(lv_obj_t *root);
void scene_draw_enter(lv_obj_t *root);
void scene_result_enter(lv_obj_t *root);
void scene_ask_enter(lv_obj_t *root);
void scene_interpret_enter(lv_obj_t *root);

#ifdef __cplusplus
}
#endif

#endif /* FORTUNE_FLOW_H */
