/**
 * @file fortune_audio.h
 * @brief 8-bit sound effects + TTS announcements via the board codec
 *        (JST PH 1.25 2P speaker). All calls are async and non-blocking.
 */
#ifndef FORTUNE_AUDIO_H
#define FORTUNE_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

/* Synthesized 8-bit style effects, values must stay below 0x80. */
typedef enum {
    FORTUNE_FX_DING = 0, /* button tap blip           */
    FORTUNE_FX_TICK,     /* small confirm             */
    FORTUNE_FX_SHAKE,    /* lot cylinder rustle       */
    FORTUNE_FX_COIN,     /* result reveal arpeggio    */
    FORTUNE_FX_BARK,     /* puppy 汪汪 while loading   */
} fortune_fx_t;

/** Open the board audio codec and start the playback worker. Returns 0
 *  on success, -1 on failure (silent degradation, everything else keeps
 *  working). */
int fortune_audio_init(void);

/** Queue a synthesized effect (non-blocking, callable from LVGL task). */
void fortune_audio_fx(fortune_fx_t fx);

/** Queue a TTS announcement: the worker fetches VolcEngine PCM from the
 *  backend and streams it to the speaker. Offline/failure = skipped.
 *  Latest wins — a new announcement replaces an unplayed pending one. */
void fortune_audio_announce(const char *text);

/** Stop the current announcement and drop any pending one (e.g. when
 *  the user leaves the result page). */
void fortune_audio_quiet(void);

/** Legacy alias: the coin arpeggio (used as the boot smoke test). */
void fortune_audio_chime(void);

#ifdef __cplusplus
}
#endif

#endif /* FORTUNE_AUDIO_H */
