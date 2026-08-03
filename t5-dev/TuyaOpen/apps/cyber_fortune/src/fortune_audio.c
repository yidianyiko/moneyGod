/**
 * @file fortune_audio.c
 * @brief 8-bit style sound effects + TTS announcements via tdl_audio
 *        (board codec, 16K mono).
 *
 * Two tiny workers so a long announcement never delays a button blip:
 *  - fx worker: synthesizes short tones/noise on demand (drops effects
 *    while speech is playing instead of garbling the stream)
 *  - speak worker: fetches VolcEngine PCM from the backend and streams
 *    it out; "latest wins" slot, abortable via fortune_audio_quiet()
 * Everything degrades silently — no speaker / no WiFi never blocks the UI.
 */

#include <math.h>
#include <string.h>
#include <stdio.h>

#include "tal_api.h"
#include "tdl_audio_manage.h"

#include "fortune_audio.h"
#include "fortune_net.h"

#define SAMPLE_RATE   16000
#define TONE_AMP      12000 /* ~37% FS, gentle on small speakers */
#define CHUNK_SAMPLES 320   /* 20ms per tdl_audio_play() call    */

/***********************************************************
 *********************** static state **********************
 ***********************************************************/
#define FX_QUEUE_LEN 8

static TDL_AUDIO_HANDLE_T s_hdl = NULL;

static SEM_HANDLE s_fx_sem = NULL;
static uint8_t s_fx_queue[FX_QUEUE_LEN];
static volatile uint8_t s_fx_head = 0;
static volatile uint8_t s_fx_tail = 0;

static SEM_HANDLE s_speak_sem = NULL;
static char s_speak_text[384];          /* latest-wins pending slot     */
static volatile int s_speak_pending = 0;
static volatile int s_speaking = 0;     /* fx are dropped while set     */
static volatile int s_speak_abort = 0;  /* quiet() stops the streaming  */

/* mic frames are not consumed yet; the callback is mandatory for open() */
static void mic_frame_cb(TDL_AUDIO_FRAME_FORMAT_E type, TDL_AUDIO_STATUS_E status, uint8_t *data, uint32_t len)
{
    (void)type;
    (void)status;
    (void)data;
    (void)len;
}

/***********************************************************
 ******************** tone synthesizers ********************
 ***********************************************************/
static void play_tone(int freq_hz, int dur_ms)
{
    static int16_t pcm[CHUNK_SAMPLES];
    int total = SAMPLE_RATE * dur_ms / 1000;
    int sent = 0;
    float step = 2.0f * (float)M_PI * (float)freq_hz / (float)SAMPLE_RATE;

    while (sent < total) {
        int n = (total - sent > CHUNK_SAMPLES) ? CHUNK_SAMPLES : (total - sent);
        for (int i = 0; i < n; i++) {
            /* short fade-in/out kills the on/off click */
            float amp = TONE_AMP;
            int pos = sent + i;
            if (pos < 160) {
                amp *= (float)pos / 160.0f;
            } else if (total - pos < 160) {
                amp *= (float)(total - pos) / 160.0f;
            }
            pcm[i] = (int16_t)(amp * sinf(step * (float)pos));
        }
        tdl_audio_play(s_hdl, (uint8_t *)pcm, n * sizeof(int16_t));
        sent += n;
    }
}

static void play_silence(int dur_ms)
{
    static int16_t pcm[CHUNK_SAMPLES];
    int total = SAMPLE_RATE * dur_ms / 1000;

    memset(pcm, 0, sizeof(pcm));
    while (total > 0) {
        int n = (total > CHUNK_SAMPLES) ? CHUNK_SAMPLES : total;
        tdl_audio_play(s_hdl, (uint8_t *)pcm, n * sizeof(int16_t));
        total -= n;
    }
}

/** Soft white-noise burst (LFSR), the "沙沙" of the lot cylinder. */
static void play_noise(int dur_ms, int amp)
{
    static int16_t pcm[CHUNK_SAMPLES];
    static uint16_t lfsr = 0xACE1;
    int total = SAMPLE_RATE * dur_ms / 1000;
    int sent = 0;

    while (sent < total) {
        int n = (total - sent > CHUNK_SAMPLES) ? CHUNK_SAMPLES : (total - sent);
        for (int i = 0; i < n; i++) {
            lfsr = (lfsr >> 1) ^ (-(lfsr & 1) & 0xB400u);
            float a = (float)amp;
            int pos = sent + i;
            if (pos < 80) {
                a *= (float)pos / 80.0f;
            } else if (total - pos < 80) {
                a *= (float)(total - pos) / 80.0f;
            }
            pcm[i] = (int16_t)(((int)(lfsr & 0x0FFF) - 2048) * a / 2048.0f);
        }
        tdl_audio_play(s_hdl, (uint8_t *)pcm, n * sizeof(int16_t));
        sent += n;
    }
}

/** One puppy "汪": a fast downward pitch chirp with a bark envelope
 *  (sharp attack, quick decay) plus a 2nd harmonic for a fleshy timbre. */
static void play_bark(void)
{
    static int16_t pcm[CHUNK_SAMPLES];
    const int total = SAMPLE_RATE * 130 / 1000; /* 130ms per yip */
    const float f_start = 1100.0f, f_end = 480.0f;
    float phase = 0.0f, phase2 = 0.0f;
    int sent = 0;

    while (sent < total) {
        int n = (total - sent > CHUNK_SAMPLES) ? CHUNK_SAMPLES : (total - sent);
        for (int i = 0; i < n; i++) {
            int pos = sent + i;
            float t = (float)pos / (float)total;
            float freq = f_start + (f_end - f_start) * t; /* downward chirp */
            phase += 2.0f * (float)M_PI * freq / (float)SAMPLE_RATE;
            phase2 += 2.0f * (float)M_PI * freq * 2.0f / (float)SAMPLE_RATE;

            /* bark envelope: 10ms attack, then exponential-ish decay */
            float env;
            if (pos < 160) {
                env = (float)pos / 160.0f;
            } else {
                float d = (float)(pos - 160) / (float)(total - 160);
                env = (1.0f - d) * (1.0f - d);
            }
            float s = sinf(phase) + 0.5f * sinf(phase2);
            pcm[i] = (int16_t)(TONE_AMP * 0.9f * env * s / 1.5f);
        }
        tdl_audio_play(s_hdl, (uint8_t *)pcm, n * sizeof(int16_t));
        sent += n;
    }
}

/***********************************************************
 ********************* effect recipes **********************
 ***********************************************************/
static void play_fx(fortune_fx_t fx)
{
    switch (fx) {
    case FORTUNE_FX_DING: /* button tap */
        play_tone(1568, 70);
        break;
    case FORTUNE_FX_TICK: /* small confirm */
        play_tone(988, 50);
        break;
    case FORTUNE_FX_SHAKE: /* lot cylinder rustle */
        for (int i = 0; i < 3; i++) {
            play_noise(90, 6000);
            play_silence(50);
        }
        break;
    case FORTUNE_FX_COIN: /* C6-E6-G6 arpeggio, "coin" flavor */
        play_tone(1047, 120);
        play_tone(1319, 120);
        play_tone(1568, 200);
        break;
    case FORTUNE_FX_BARK: /* two puppy yips: 汪汪 */
        play_bark();
        play_silence(110);
        play_bark();
        break;
    default:
        break;
    }
}

static void fx_worker_thread(void *arg)
{
    (void)arg;

    for (;;) {
        tal_semaphore_wait(s_fx_sem, SEM_WAIT_FOREVER);

        fortune_fx_t fx = (fortune_fx_t)s_fx_queue[s_fx_head];
        s_fx_head = (s_fx_head + 1) % FX_QUEUE_LEN;

        if (s_speaking) {
            continue; /* don't garble the announcement */
        }
        play_fx(fx);
    }
}

/***********************************************************
 ******************** TTS announcement *********************
 ***********************************************************/
static void speak_worker_thread(void *arg)
{
    (void)arg;
    char text[sizeof(s_speak_text)];

    for (;;) {
        tal_semaphore_wait(s_speak_sem, SEM_WAIT_FOREVER);

        if (!s_speak_pending) {
            continue; /* superseded by a newer announcement */
        }
        strncpy(text, s_speak_text, sizeof(text) - 1);
        text[sizeof(text) - 1] = '\0';
        s_speak_pending = 0;
        s_speak_abort = 0;

        if (!fortune_net_is_up()) {
            PR_NOTICE("[fortune-audio] offline, announcement skipped");
            continue;
        }

        uint8_t *pcm = NULL;
        uint32_t len = 0;
        if (fortune_net_tts_fetch(text, &pcm, &len) != 0 || pcm == NULL) {
            PR_NOTICE("[fortune-audio] tts fetch failed, announcement skipped");
            continue;
        }
        PR_NOTICE("[fortune-audio] speaking %u bytes of PCM", (unsigned)len);

        s_speaking = 1;
        uint32_t off = 0;
        while (off < len && !s_speak_abort) {
            uint32_t n = len - off;
            if (n > CHUNK_SAMPLES * sizeof(int16_t)) {
                n = CHUNK_SAMPLES * sizeof(int16_t);
            }
            tdl_audio_play(s_hdl, pcm + off, n);
            off += n;
        }
        s_speaking = 0;
        tal_free(pcm);
    }
}

/***********************************************************
 ************************ public API ***********************
 ***********************************************************/
int fortune_audio_init(void)
{
    OPERATE_RET rt = tdl_audio_find(AUDIO_CODEC_NAME, &s_hdl);
    if (rt != OPRT_OK || s_hdl == NULL) {
        PR_ERR("[fortune-audio] codec not found %d, silent mode", rt);
        s_hdl = NULL;
        return -1;
    }
    rt = tdl_audio_open(s_hdl, mic_frame_cb);
    if (rt != OPRT_OK) {
        PR_ERR("[fortune-audio] codec open failed %d, silent mode", rt);
        s_hdl = NULL;
        return -1;
    }
    tdl_audio_volume_set(s_hdl, 80);

    tal_semaphore_create_init(&s_fx_sem, 0, FX_QUEUE_LEN);
    tal_semaphore_create_init(&s_speak_sem, 0, 4);

    static THREAD_HANDLE fx_thrd = NULL;
    THREAD_CFG_T cfg;
    memset(&cfg, 0, sizeof(THREAD_CFG_T));
    cfg.stackDepth = 1024 * 4;
    cfg.priority   = THREAD_PRIO_2;
    cfg.thrdname   = "fortune_fx";
    tal_thread_create_and_start(&fx_thrd, NULL, NULL, fx_worker_thread, NULL, &cfg);

    static THREAD_HANDLE speak_thrd = NULL;
    memset(&cfg, 0, sizeof(THREAD_CFG_T));
    cfg.stackDepth = 1024 * 8; /* blocking HTTP fetch runs here */
    cfg.priority   = THREAD_PRIO_2;
    cfg.thrdname   = "fortune_speak";
    tal_thread_create_and_start(&speak_thrd, NULL, NULL, speak_worker_thread, NULL, &cfg);

    PR_NOTICE("[fortune-audio] codec open, speaker ready");
    return 0;
}

void fortune_audio_fx(fortune_fx_t fx)
{
    if (s_hdl == NULL || s_fx_sem == NULL) {
        return; /* silent mode */
    }
    s_fx_queue[s_fx_tail] = (uint8_t)fx;
    s_fx_tail = (s_fx_tail + 1) % FX_QUEUE_LEN;
    tal_semaphore_post(s_fx_sem);
}

void fortune_audio_announce(const char *text)
{
    if (s_hdl == NULL || s_speak_sem == NULL || text == NULL || text[0] == '\0') {
        return;
    }
    strncpy(s_speak_text, text, sizeof(s_speak_text) - 1);
    s_speak_text[sizeof(s_speak_text) - 1] = '\0';
    s_speak_pending = 1;
    tal_semaphore_post(s_speak_sem);
}

void fortune_audio_quiet(void)
{
    s_speak_pending = 0;
    s_speak_abort = 1;
}

void fortune_audio_chime(void)
{
    fortune_audio_fx(FORTUNE_FX_COIN);
}
