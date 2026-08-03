/**
 * @file fortune_net.h
 * @brief WiFi bring-up + async POST /api/fortune/draw against core-engine.
 */
#ifndef FORTUNE_NET_H
#define FORTUNE_NET_H

#include <stddef.h>

#include "fortune_data.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FORTUNE_NET_IDLE = 0,
    FORTUNE_NET_BUSY,
    FORTUNE_NET_DONE,   /* result ready via fortune_net_take_result() */
    FORTUNE_NET_FAILED, /* request finished with error, use fallback  */
} fortune_net_state_t;

/** Bring up WiFi (netmgr) and start the request worker thread. Call once. */
void fortune_net_init(void);

/** True once EVENT_LINK_STATUS_CHG reported link-up. */
int fortune_net_is_up(void);

/**
 * @brief Kick an async draw request (non-blocking, call from LVGL task).
 *        Any previous unfinished request result is discarded.
 * @return 0 if queued, <0 if the worker is unavailable/busy.
 */
int fortune_net_draw_async(const char *category, const char *question, const char *answer);

/** Poll the request state (call from LVGL task). */
fortune_net_state_t fortune_net_poll(void);

/**
 * @brief Copy the finished result out and reset state to IDLE.
 * @return 0 on success, <0 if no result is pending.
 */
int fortune_net_take_result(fortune_result_t *out);

/* --- 解签互动: yes/no question + targeted interpretation ------------- */

/** Kick an async POST /api/fortune/question (prefetched from the result
 *  scene). Poem/grade/lot are copied from @p res. */
int fortune_net_ask_async(const fortune_result_t *res, const char *category);
fortune_net_state_t fortune_net_ask_poll(void);
/** Copy the question text out and reset the ask state to IDLE. */
int fortune_net_take_ask(char *out, size_t out_size);

/** Kick an async POST /api/fortune/interpret with the user's yes/no. */
int fortune_net_interpret_async(const fortune_result_t *res, const char *category,
                                const char *ask, int reply_yes);
fortune_net_state_t fortune_net_interpret_poll(void);
/** Copy the interpretation text out and reset the state to IDLE. */
int fortune_net_take_interpret(char *out, size_t out_size);

/* --- TTS ------------------------------------------------------------- */

/**
 * @brief Blocking POST /api/fortune/tts, returns raw 16K mono PCM.
 *        Call from a worker thread only (the audio worker), never from
 *        the LVGL task. On success the caller owns *pcm_out and must
 *        release it with tal_free().
 * @return 0 on success, <0 on any failure (*pcm_out stays NULL).
 */
int fortune_net_tts_fetch(const char *text, uint8_t **pcm_out, uint32_t *len_out);

#ifdef __cplusplus
}
#endif

#endif /* FORTUNE_NET_H */
