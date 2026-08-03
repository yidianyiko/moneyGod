/**
 * @file fortune_net.c
 * @brief WiFi bring-up + async draw request against the core-engine backend.
 *
 * Same worker-thread-plus-semaphore shape as fortune_printer.c: the LVGL
 * task only copies the request params and posts; the worker does the
 * blocking HTTP round-trip and JSON parsing, then flips the state flag
 * that the draw scene polls.
 */

#include <string.h>
#include <stdio.h>

#include "tal_api.h"
#include "tuya_cloud_types.h"
#include "http_client_interface.h"
#include "netmgr.h"
#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
#include "netconn_wifi.h"
#endif
#if defined(ENABLE_LIBLWIP) && (ENABLE_LIBLWIP == 1)
#include "lwip/lwip_init.h"
#endif
#include "cJSON.h"

#include "fortune_net.h"

/***********************************************************
 ************ deployment settings — 烧录前修改 **************
 ***********************************************************/
#define FORTUNE_WIFI_SSID "ADVX-Players"
#define FORTUNE_WIFI_PSWD "AdventureX"

/* backup WiFi — used automatically when the primary fails to connect */
#define FORTUNE_WIFI_BAK_SSID "."
#define FORTUNE_WIFI_BAK_PSWD "zxc123456"

/* how often to re-check the link and rotate to the next credential */
#define FORTUNE_WIFI_RETRY_MS (15 * 1000)

#define FORTUNE_API_HOST  "47.98.99.199"
#define FORTUNE_API_PORT  8000
#define FORTUNE_API_PATH  "/api/fortune/draw"
#define FORTUNE_ASK_PATH  "/api/fortune/question"
#define FORTUNE_ITP_PATH  "/api/fortune/interpret"
#define FORTUNE_TTS_PATH  "/api/fortune/tts"
/* Must match FORTUNE_API_TOKEN in core-engine/.env; empty = no auth header. */
#define FORTUNE_API_TOKEN ""

#define FORTUNE_HTTP_TIMEOUT_MS (10 * 1000)
#define FORTUNE_TTS_TIMEOUT_MS  (20 * 1000) /* several-second PCM bodies */

/***********************************************************
 *********************** static state **********************
 ***********************************************************/
/* worker request kinds, pushed through a small ring in post order */
typedef enum {
    REQ_KIND_DRAW = 0,
    REQ_KIND_ASK,
    REQ_KIND_INTERPRET,
} req_kind_t;

#define REQ_QUEUE_LEN 4

static SEM_HANDLE s_req_sem = NULL;
static uint8_t s_req_queue[REQ_QUEUE_LEN];
static volatile uint8_t s_req_head = 0; /* pop index (worker)      */
static volatile uint8_t s_req_tail = 0; /* push index (LVGL task)  */
static volatile fortune_net_state_t s_state = FORTUNE_NET_IDLE;
static volatile fortune_net_state_t s_ask_state = FORTUNE_NET_IDLE;
static volatile fortune_net_state_t s_itp_state = FORTUNE_NET_IDLE;
static volatile int s_link_up = 0;

/* WiFi candidates tried in order; the retry timer rotates on link-down */
typedef struct {
    const char *ssid;
    const char *pswd;
} wifi_cred_t;

static const wifi_cred_t s_wifi_creds[] = {
    {FORTUNE_WIFI_SSID, FORTUNE_WIFI_PSWD},
    {FORTUNE_WIFI_BAK_SSID, FORTUNE_WIFI_BAK_PSWD},
};
#define WIFI_CRED_COUNT (sizeof(s_wifi_creds) / sizeof(s_wifi_creds[0]))

static uint8_t s_wifi_idx = 0;
static TIMER_ID s_wifi_timer = NULL;

/* request params (copied on *_async, read by the worker) */
static char s_category[32];
static char s_question[128];
static char s_answer[96];

/* 解签 params: lot context + question + user's yes/no */
static fortune_result_t s_ctx_res;
static char s_ctx_category[32];
static char s_itp_ask[192];
static int s_itp_reply_yes = 0;

static fortune_result_t s_result;
static char s_ask_text[192];
static char s_itp_text[640];

/***********************************************************
 ******************** response handling ********************
 ***********************************************************/
static void copy_json_str(char *dst, size_t dst_size, const cJSON *item)
{
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        strncpy(dst, item->valuestring, dst_size - 1);
        dst[dst_size - 1] = '\0';
    }
}

/** Parse the backend JSON body into s_result. Returns 0 on success. */
static int parse_response(const char *body)
{
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        PR_ERR("[fortune-net] response is not valid JSON");
        return -1;
    }

    int rc = -1;
    cJSON *poem = cJSON_GetObjectItem(root, "poem");
    cJSON *grade = cJSON_GetObjectItem(root, "grade");
    cJSON *expl = cJSON_GetObjectItem(root, "explanation");
    cJSON *advice = cJSON_GetObjectItem(root, "advice");
    cJSON *lot_no = cJSON_GetObjectItem(root, "lot_no");
    cJSON *score = cJSON_GetObjectItem(root, "grade_score");

    if (cJSON_IsArray(poem) && cJSON_GetArraySize(poem) >= 4 &&
        cJSON_IsString(grade) && cJSON_IsString(expl) && cJSON_IsString(advice)) {
        memset(&s_result, 0, sizeof(s_result));
        s_result.lot_no = cJSON_IsNumber(lot_no) ? lot_no->valueint : 1;
        s_result.grade_score = cJSON_IsNumber(score) ? score->valueint : 3;
        copy_json_str(s_result.grade, sizeof(s_result.grade), grade);
        for (int i = 0; i < 4; i++) {
            copy_json_str(s_result.poem[i], sizeof(s_result.poem[i]), cJSON_GetArrayItem(poem, i));
        }
        copy_json_str(s_result.explanation, sizeof(s_result.explanation), expl);
        copy_json_str(s_result.advice, sizeof(s_result.advice), advice);
        rc = 0;
    } else {
        PR_ERR("[fortune-net] response missing required fields");
    }

    cJSON_Delete(root);
    return rc;
}

/** One blocking POST round-trip; hands a 200 body to @p parse. */
static int post_json(const char *path, char *body, int (*parse)(const char *))
{
    int rc = -1;

    http_client_header_t headers[] = {
        {.key = "Content-Type", .value = "application/json"},
        {.key = "X-Fortune-Token", .value = FORTUNE_API_TOKEN},
    };
    uint8_t headers_count = sizeof(headers) / sizeof(headers[0]);
    if (FORTUNE_API_TOKEN[0] == '\0') {
        headers_count = 1; /* token header only when configured */
    }

    http_client_response_t response = {0};
    http_client_status_t status =
        http_client_request(&(const http_client_request_t){.host = FORTUNE_API_HOST,
                                                           .port = FORTUNE_API_PORT,
                                                           .path = path,
                                                           .method = "POST",
                                                           .headers = headers,
                                                           .headers_count = headers_count,
                                                           .body = (const uint8_t *)body,
                                                           .body_length = strlen(body),
                                                           .timeout_ms = FORTUNE_HTTP_TIMEOUT_MS},
                            &response);

    if (status == HTTP_CLIENT_SUCCESS && response.status_code == 200 && response.body != NULL) {
        rc = parse((const char *)response.body);
    } else {
        PR_ERR("[fortune-net] %s failed: status=%d http=%d", path, status, response.status_code);
    }

    http_client_free(&response);
    cJSON_free(body);
    return rc;
}

/** Draw request round-trip. Returns 0 and fills s_result on success. */
static int do_request(void)
{
    cJSON *req = cJSON_CreateObject();
    if (req == NULL) {
        return -1;
    }
    cJSON_AddStringToObject(req, "category", s_category);
    cJSON_AddStringToObject(req, "question", s_question);
    cJSON_AddStringToObject(req, "answer", s_answer);
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (body == NULL) {
        return -1;
    }
    return post_json(FORTUNE_API_PATH, body, parse_response);
}

/* --- 解签: question + interpret round-trips ------------------------- */

static int parse_ask(const char *body)
{
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        return -1;
    }
    int rc = -1;
    cJSON *ask = cJSON_GetObjectItem(root, "ask");
    if (cJSON_IsString(ask) && ask->valuestring[0] != '\0') {
        copy_json_str(s_ask_text, sizeof(s_ask_text), ask);
        rc = 0;
    }
    cJSON_Delete(root);
    return rc;
}

static int parse_interpret(const char *body)
{
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        return -1;
    }
    int rc = -1;
    cJSON *itp = cJSON_GetObjectItem(root, "interpret");
    if (cJSON_IsString(itp) && itp->valuestring[0] != '\0') {
        copy_json_str(s_itp_text, sizeof(s_itp_text), itp);
        rc = 0;
    }
    cJSON_Delete(root);
    return rc;
}

/** Shared lot-context body: lot_no/grade/poem/category (+ask/reply). */
static char *build_ctx_body(const char *ask, const char *reply)
{
    cJSON *req = cJSON_CreateObject();
    if (req == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(req, "lot_no", s_ctx_res.lot_no);
    cJSON_AddStringToObject(req, "grade", s_ctx_res.grade);
    cJSON *poem = cJSON_CreateArray();
    for (int i = 0; i < 4; i++) {
        cJSON_AddItemToArray(poem, cJSON_CreateString(s_ctx_res.poem[i]));
    }
    cJSON_AddItemToObject(req, "poem", poem);
    cJSON_AddStringToObject(req, "category", s_ctx_category);
    if (ask != NULL) {
        cJSON_AddStringToObject(req, "ask", ask);
        cJSON_AddStringToObject(req, "reply", reply);
    }
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    return body;
}

static int do_ask_request(void)
{
    char *body = build_ctx_body(NULL, NULL);
    return (body != NULL) ? post_json(FORTUNE_ASK_PATH, body, parse_ask) : -1;
}

static int do_interpret_request(void)
{
    char *body = build_ctx_body(s_itp_ask, s_itp_reply_yes ? "yes" : "no");
    return (body != NULL) ? post_json(FORTUNE_ITP_PATH, body, parse_interpret) : -1;
}

/***********************************************************
 ********************** worker thread **********************
 ***********************************************************/
static void net_worker_thread(void *arg)
{
    (void)arg;

    for (;;) {
        tal_semaphore_wait(s_req_sem, SEM_WAIT_FOREVER);

        req_kind_t kind = (req_kind_t)s_req_queue[s_req_head];
        s_req_head = (s_req_head + 1) % REQ_QUEUE_LEN;

        switch (kind) {
        case REQ_KIND_DRAW:
            if (do_request() == 0) {
                s_state = FORTUNE_NET_DONE;
                PR_NOTICE("[fortune-net] lot %d (%s) received", s_result.lot_no, s_result.grade);
            } else {
                s_state = FORTUNE_NET_FAILED;
            }
            break;
        case REQ_KIND_ASK:
            if (do_ask_request() == 0) {
                s_ask_state = FORTUNE_NET_DONE;
                PR_NOTICE("[fortune-net] ask received: %s", s_ask_text);
            } else {
                s_ask_state = FORTUNE_NET_FAILED;
            }
            break;
        case REQ_KIND_INTERPRET:
            if (do_interpret_request() == 0) {
                s_itp_state = FORTUNE_NET_DONE;
                PR_NOTICE("[fortune-net] interpret received (%d bytes)", (int)strlen(s_itp_text));
            } else {
                s_itp_state = FORTUNE_NET_FAILED;
            }
            break;
        default:
            break;
        }
    }
}

/***********************************************************
 ********************* link status event *******************
 ***********************************************************/
#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
/** Push the credential at @p idx to the network manager. */
static void wifi_apply_cred(uint8_t idx)
{
    netconn_wifi_info_t wifi_info = {0};
    strcpy(wifi_info.ssid, s_wifi_creds[idx].ssid);
    strcpy(wifi_info.pswd, s_wifi_creds[idx].pswd);
    netmgr_conn_set(NETCONN_WIFI, NETCONN_CMD_SSID_PSWD, &wifi_info);
    PR_NOTICE("[fortune-net] trying WiFi[%u] ssid=\"%s\"", idx, s_wifi_creds[idx].ssid);
}

/** Periodic: while the link is down, rotate to the next credential. */
static void wifi_retry_timer_cb(TIMER_ID timer_id, void *arg)
{
    (void)timer_id;
    (void)arg;
    if (s_link_up || WIFI_CRED_COUNT <= 1) {
        return; /* connected (or nothing to rotate to) */
    }
    s_wifi_idx = (s_wifi_idx + 1) % WIFI_CRED_COUNT;
    wifi_apply_cred(s_wifi_idx);
}
#endif

static OPERATE_RET link_status_cb(void *data)
{
    netmgr_status_e status = *((netmgr_status_e *)data);
    s_link_up = (status != NETMGR_LINK_DOWN);
    PR_NOTICE("[fortune-net] link status: %d", status);
    return OPRT_OK;
}

/***********************************************************
 ************************ public API ***********************
 ***********************************************************/
void fortune_net_init(void)
{
    static THREAD_HANDLE thrd = NULL;

    if (s_req_sem != NULL) {
        return;
    }

    /* network stack bring-up (same order as the http_client example) */
    tal_kv_init(&(tal_kv_cfg_t){
        .seed = "vmlkasdh93dlvlcy",
        .key = "dflfuap134ddlduq",
    });
    tal_sw_timer_init();
    tal_workq_init();
    tal_event_subscribe(EVENT_LINK_STATUS_CHG, "fortune_net", link_status_cb, SUBSCRIBE_TYPE_NORMAL);

#if defined(ENABLE_LIBLWIP) && (ENABLE_LIBLWIP == 1)
    TUYA_LwIP_Init();
#endif

    netmgr_init(NETCONN_WIFI);

#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
    s_wifi_idx = 0;
    wifi_apply_cred(s_wifi_idx);
    /* auto-fallback to the backup WiFi if the primary never links up */
    if (WIFI_CRED_COUNT > 1 &&
        tal_sw_timer_create(wifi_retry_timer_cb, NULL, &s_wifi_timer) == OPRT_OK) {
        tal_sw_timer_start(s_wifi_timer, FORTUNE_WIFI_RETRY_MS, TAL_TIMER_CYCLE);
    }
#endif

    /* request worker */
    tal_semaphore_create_init(&s_req_sem, 0, 4);

    THREAD_CFG_T cfg;
    memset(&cfg, 0, sizeof(THREAD_CFG_T));
    cfg.stackDepth = 1024 * 8; /* TLS-free HTTP + cJSON */
    cfg.priority   = THREAD_PRIO_2;
    cfg.thrdname   = "fortune_net";
    tal_thread_create_and_start(&thrd, NULL, NULL, net_worker_thread, NULL, &cfg);
}

int fortune_net_is_up(void)
{
    return s_link_up;
}

/** Push a request kind into the ring and wake the worker. */
static void post_req(req_kind_t kind)
{
    s_req_queue[s_req_tail] = (uint8_t)kind;
    s_req_tail = (s_req_tail + 1) % REQ_QUEUE_LEN;
    tal_semaphore_post(s_req_sem);
}

int fortune_net_draw_async(const char *category, const char *question, const char *answer)
{
    if (s_req_sem == NULL || category == NULL) {
        return -1;
    }
    if (s_state == FORTUNE_NET_BUSY) {
        return -2;
    }

    strncpy(s_category, category, sizeof(s_category) - 1);
    s_category[sizeof(s_category) - 1] = '\0';
    strncpy(s_question, question ? question : "", sizeof(s_question) - 1);
    s_question[sizeof(s_question) - 1] = '\0';
    strncpy(s_answer, answer ? answer : "", sizeof(s_answer) - 1);
    s_answer[sizeof(s_answer) - 1] = '\0';

    s_state = FORTUNE_NET_BUSY;
    post_req(REQ_KIND_DRAW);
    return 0;
}

fortune_net_state_t fortune_net_poll(void)
{
    return s_state;
}

int fortune_net_take_result(fortune_result_t *out)
{
    if (out == NULL || s_state != FORTUNE_NET_DONE) {
        return -1;
    }
    memcpy(out, &s_result, sizeof(fortune_result_t));
    s_state = FORTUNE_NET_IDLE;
    return 0;
}

/* --- 解签: yes/no question + targeted interpretation ----------------- */

/** Copy the lot context (shared by ask/interpret request bodies). */
static void copy_ctx(const fortune_result_t *res, const char *category)
{
    memcpy(&s_ctx_res, res, sizeof(s_ctx_res));
    strncpy(s_ctx_category, category ? category : "今日运势", sizeof(s_ctx_category) - 1);
    s_ctx_category[sizeof(s_ctx_category) - 1] = '\0';
}

int fortune_net_ask_async(const fortune_result_t *res, const char *category)
{
    if (s_req_sem == NULL || res == NULL) {
        return -1;
    }
    if (s_ask_state == FORTUNE_NET_BUSY) {
        return -2;
    }
    copy_ctx(res, category);
    s_ask_state = FORTUNE_NET_BUSY;
    post_req(REQ_KIND_ASK);
    return 0;
}

fortune_net_state_t fortune_net_ask_poll(void)
{
    return s_ask_state;
}

int fortune_net_take_ask(char *out, size_t out_size)
{
    if (out == NULL || out_size == 0 || s_ask_state != FORTUNE_NET_DONE) {
        return -1;
    }
    strncpy(out, s_ask_text, out_size - 1);
    out[out_size - 1] = '\0';
    s_ask_state = FORTUNE_NET_IDLE;
    return 0;
}

int fortune_net_interpret_async(const fortune_result_t *res, const char *category,
                                const char *ask, int reply_yes)
{
    if (s_req_sem == NULL || res == NULL || ask == NULL) {
        return -1;
    }
    if (s_itp_state == FORTUNE_NET_BUSY) {
        return -2;
    }
    copy_ctx(res, category);
    strncpy(s_itp_ask, ask, sizeof(s_itp_ask) - 1);
    s_itp_ask[sizeof(s_itp_ask) - 1] = '\0';
    s_itp_reply_yes = reply_yes ? 1 : 0;
    s_itp_state = FORTUNE_NET_BUSY;
    post_req(REQ_KIND_INTERPRET);
    return 0;
}

fortune_net_state_t fortune_net_interpret_poll(void)
{
    return s_itp_state;
}

int fortune_net_take_interpret(char *out, size_t out_size)
{
    if (out == NULL || out_size == 0 || s_itp_state != FORTUNE_NET_DONE) {
        return -1;
    }
    strncpy(out, s_itp_text, out_size - 1);
    out[out_size - 1] = '\0';
    s_itp_state = FORTUNE_NET_IDLE;
    return 0;
}

/* --- TTS: blocking PCM fetch (called from the audio worker thread) --- */

int fortune_net_tts_fetch(const char *text, uint8_t **pcm_out, uint32_t *len_out)
{
    if (text == NULL || pcm_out == NULL || len_out == NULL) {
        return -1;
    }
    *pcm_out = NULL;
    *len_out = 0;

    cJSON *req = cJSON_CreateObject();
    if (req == NULL) {
        return -1;
    }
    cJSON_AddStringToObject(req, "text", text);
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (body == NULL) {
        return -1;
    }

    http_client_header_t headers[] = {
        {.key = "Content-Type", .value = "application/json"},
        {.key = "X-Fortune-Token", .value = FORTUNE_API_TOKEN},
    };
    uint8_t headers_count = sizeof(headers) / sizeof(headers[0]);
    if (FORTUNE_API_TOKEN[0] == '\0') {
        headers_count = 1;
    }

    int rc = -1;
    http_client_response_t response = {0};
    http_client_status_t status =
        http_client_request(&(const http_client_request_t){.host = FORTUNE_API_HOST,
                                                           .port = FORTUNE_API_PORT,
                                                           .path = FORTUNE_TTS_PATH,
                                                           .method = "POST",
                                                           .headers = headers,
                                                           .headers_count = headers_count,
                                                           .body = (const uint8_t *)body,
                                                           .body_length = strlen(body),
                                                           .timeout_ms = FORTUNE_TTS_TIMEOUT_MS},
                            &response);

    if (status == HTTP_CLIENT_SUCCESS && response.status_code == 200 &&
        response.body != NULL && response.body_length > 0) {
        /* hand the body straight to the caller, skip http_client_free()
         * for it — the caller releases with tal_free() when done */
        *pcm_out = (uint8_t *)response.body;
        *len_out = (uint32_t)response.body_length;
        response.body = NULL;
        rc = 0;
    } else {
        PR_ERR("[fortune-net] tts failed: status=%d http=%d", status, response.status_code);
    }

    http_client_free(&response);
    cJSON_free(body);
    return rc;
}
