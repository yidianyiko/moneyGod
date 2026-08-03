/**
 * @file fortune_printer.c
 * @brief Cyber Fortune Temple — prints the drawn lot as an ESC/POS ticket
 *        on the EM5820H USB thermal printer (58mm, GBK Chinese font).
 *
 * A tiny worker thread waits on a semaphore; the LVGL task only copies the
 * result into a pending slot and posts, so the UI never blocks on USB I/O.
 */

#include <string.h>
#include <stdio.h>

#include "tal_api.h"
#include "fortune_printer.h"

/* Beken CherryUSB host printer class driver (bk_usbh_printer). */
extern int bk_usbh_printer_is_connected(void);
extern int bk_usbh_printer_write(const uint8_t *buffer, uint32_t buflen, uint32_t timeout_ms);

/* Auto-generated Unicode->GBK table (src/assets/fortune_gbk_map.c). */
typedef struct {
    uint16_t unicode;
    uint8_t  gbk_hi;
    uint8_t  gbk_lo;
} gbk_map_entry_t;
extern const gbk_map_entry_t g_gbk_map[];
extern const int g_gbk_map_count;

/* Mascot raster for the ticket header (src/assets/fortune_ticket_logo.c). */
extern const int g_ticket_logo_width_bytes;
extern const int g_ticket_logo_height;
extern const uint8_t g_ticket_logo_data[];

/***********************************************************
 *********************** static state **********************
 ***********************************************************/
/* QR on the ticket footer: plain-text contact card (扫码显示定制微信).
 * Emitted as raw UTF-8 into the QR payload — scanners decode UTF-8, so this
 * must NOT go through emit_utf8()'s GBK conversion. */
#define FORTUNE_QR_TEXT "定制请添加微信 yidianyiko12138"

static SEM_HANDLE s_print_sem = NULL;
static fortune_result_t s_pending;      /* copy owned by the worker      */
static fortune_extra_t s_pending_extra; /* 解签 add-on, empty = absent   */
static volatile int s_busy = 0;         /* set by print(), cleared after */

static uint8_t s_ticket[12288];
static uint32_t s_ticket_len;

/***********************************************************
 ******************* ticket build helpers ******************
 ***********************************************************/
static void emit_raw(const uint8_t *data, uint32_t len)
{
    if (s_ticket_len + len <= sizeof(s_ticket)) {
        memcpy(&s_ticket[s_ticket_len], data, len);
        s_ticket_len += len;
    }
}

static void emit_byte(uint8_t b)
{
    emit_raw(&b, 1);
}

static int gbk_lookup(uint16_t unicode, uint8_t *hi, uint8_t *lo)
{
    int l = 0, r = g_gbk_map_count - 1;
    while (l <= r) {
        int m = (l + r) / 2;
        if (g_gbk_map[m].unicode == unicode) {
            *hi = g_gbk_map[m].gbk_hi;
            *lo = g_gbk_map[m].gbk_lo;
            return 0;
        }
        if (g_gbk_map[m].unicode < unicode) {
            l = m + 1;
        } else {
            r = m - 1;
        }
    }
    return -1;
}

/** UTF-8 text -> GBK bytes into the ticket buffer (ASCII passes through). */
static void emit_utf8(const char *s)
{
    const uint8_t *p = (const uint8_t *)s;
    while (*p) {
        uint32_t cp;
        if (p[0] < 0x80) {
            cp = p[0];
            p += 1;
        } else if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
            cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
            p += 2;
        } else if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
            cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            p += 3;
        } else {
            p += 1; /* invalid / 4-byte sequences: skip */
            continue;
        }

        if (cp < 0x80) {
            emit_byte((uint8_t)cp);
        } else {
            uint8_t hi, lo;
            if (gbk_lookup((uint16_t)cp, &hi, &lo) == 0) {
                emit_byte(hi);
                emit_byte(lo);
            }
            /* chars missing from the map are dropped */
        }
    }
}

static void emit_divider(void)
{
    for (int i = 0; i < 32; i++) {
        emit_byte('-');
    }
    emit_byte(0x0A);
}

/* ESC/POS shorthands */
static void esc_init(void)          { emit_raw((const uint8_t[]){0x1B, 0x40}, 2); }
static void esc_align(uint8_t n)    { emit_raw((const uint8_t[]){0x1B, 0x61, n}, 3); }
static void gs_size(uint8_t n)      { emit_raw((const uint8_t[]){0x1D, 0x21, n}, 3); }

/** Pre-centered 1-bit mascot raster via GS v 0 (normal density). */
static void emit_logo(void)
{
    uint16_t xb = (uint16_t)g_ticket_logo_width_bytes;
    uint16_t yd = (uint16_t)g_ticket_logo_height;

    emit_raw((const uint8_t[]){0x1D, 0x76, 0x30, 0x00, (uint8_t)(xb & 0xFF), (uint8_t)(xb >> 8),
                               (uint8_t)(yd & 0xFF), (uint8_t)(yd >> 8)},
             8);
    emit_raw(g_ticket_logo_data, (uint32_t)xb * yd);
}

/** ESC/POS native QR (GS ( k): model 2, size 5, EC level M, then print.
 *  EM5820H honours these; if a unit ever ignores them the section just
 *  comes out blank and the caption line below still tells the story. */
static void emit_qr(const char *payload)
{
    uint16_t len = (uint16_t)strlen(payload) + 3;

    /* model 2 */
    emit_raw((const uint8_t[]){0x1D, 0x28, 0x6B, 0x04, 0x00, 0x31, 0x41, 0x32, 0x00}, 9);
    /* module size 5 dots */
    emit_raw((const uint8_t[]){0x1D, 0x28, 0x6B, 0x03, 0x00, 0x31, 0x43, 0x05}, 8);
    /* error correction level M */
    emit_raw((const uint8_t[]){0x1D, 0x28, 0x6B, 0x03, 0x00, 0x31, 0x45, 0x31}, 8);
    /* store data */
    emit_raw((const uint8_t[]){0x1D, 0x28, 0x6B, (uint8_t)(len & 0xFF), (uint8_t)(len >> 8), 0x31,
                               0x50, 0x30},
             8);
    emit_raw((const uint8_t *)payload, (uint32_t)strlen(payload));
    /* print stored symbol */
    emit_raw((const uint8_t[]){0x1D, 0x28, 0x6B, 0x03, 0x00, 0x31, 0x51, 0x30}, 8);
}

/***********************************************************
 ********************* ticket layout ***********************
 ***********************************************************/
static void build_ticket(const fortune_result_t *res, const fortune_extra_t *extra)
{
    char line[80];

    s_ticket_len = 0;
    esc_init();

    /* header */
    esc_align(1);
    gs_size(0x11); /* double width + height */
    emit_utf8("赛博财神庙");
    emit_byte(0x0A);
    gs_size(0x00);
    emit_byte(0x0A);

    /* mascot holding the freshly drawn lot */
    emit_logo();
    emit_byte(0x0A);

    /* lot number + grade (double height) */
    gs_size(0x01);
    snprintf(line, sizeof(line), "第 %d 签 · %s", res->lot_no, res->grade);
    emit_utf8(line);
    emit_byte(0x0A);
    gs_size(0x00);
    emit_divider();

    /* poem (4 lines, centered) */
    for (int i = 0; i < 4; i++) {
        emit_utf8(res->poem[i]);
        emit_byte(0x0A);
    }
    emit_divider();

    /* explanation + advice (left aligned, printer wraps) */
    esc_align(0);
    emit_utf8("解签：");
    emit_utf8(res->explanation);
    emit_byte(0x0A);
    emit_byte(0x0A);
    emit_utf8("指点：");
    emit_utf8(res->advice);
    emit_byte(0x0A);

    /* 解签互动: question, answer and the targeted reading */
    if (extra != NULL && extra->interpret[0] != '\0') {
        esc_align(1);
        emit_divider();
        emit_utf8("— 财神问答 —");
        emit_byte(0x0A);
        esc_align(0);
        emit_utf8("问：");
        emit_utf8(extra->ask);
        emit_byte(0x0A);
        emit_utf8("答：");
        emit_utf8(extra->reply_yes ? "是" : "否");
        emit_byte(0x0A);
        emit_byte(0x0A);
        emit_utf8("财神说：");
        emit_utf8(extra->interpret);
        emit_byte(0x0A);
    }

    /* footer: blessing + contact QR */
    esc_align(1);
    emit_divider();
    emit_utf8("诚心求签 运随心转");
    emit_byte(0x0A);
    emit_byte(0x0A);
    emit_qr(FORTUNE_QR_TEXT);
    emit_utf8("扫码加微信 定制专属财神");
    emit_byte(0x0A);

    /* feed past the tear bar */
    for (int i = 0; i < 5; i++) {
        emit_byte(0x0A);
    }
}

/***********************************************************
 ********************** worker thread **********************
 ***********************************************************/
static void print_worker_thread(void *arg)
{
    (void)arg;

    for (;;) {
        tal_semaphore_wait(s_print_sem, SEM_WAIT_FOREVER);

        if (!bk_usbh_printer_is_connected()) {
            PR_NOTICE("[fortune-printer] no printer attached, ticket skipped (lot %d)", s_pending.lot_no);
            s_busy = 0;
            continue;
        }

        build_ticket(&s_pending, &s_pending_extra);
        int ret = bk_usbh_printer_write(s_ticket, s_ticket_len, 8000);
        PR_NOTICE("[fortune-printer] lot %d ticket: %u bytes built, %d sent", s_pending.lot_no,
                  (unsigned)s_ticket_len, ret);
        s_busy = 0;
    }
}

/***********************************************************
 ************************ public API ***********************
 ***********************************************************/
void fortune_printer_init(void)
{
    static THREAD_HANDLE thrd = NULL;
    THREAD_CFG_T cfg;

    if (s_print_sem != NULL) {
        return;
    }
    tal_semaphore_create_init(&s_print_sem, 0, 8);

    memset(&cfg, 0, sizeof(THREAD_CFG_T));
    cfg.stackDepth = 1024 * 4;
    cfg.priority   = THREAD_PRIO_2;
    cfg.thrdname   = "fortune_print";
    tal_thread_create_and_start(&thrd, NULL, NULL, print_worker_thread, NULL, &cfg);
}

void fortune_printer_print(const fortune_result_t *result)
{
    fortune_printer_print_full(result, NULL);
}

void fortune_printer_print_full(const fortune_result_t *result, const fortune_extra_t *extra)
{
    if (s_print_sem == NULL || result == NULL || s_busy) {
        return;
    }
    memcpy(&s_pending, result, sizeof(fortune_result_t));
    if (extra != NULL) {
        memcpy(&s_pending_extra, extra, sizeof(fortune_extra_t));
    } else {
        memset(&s_pending_extra, 0, sizeof(fortune_extra_t));
    }
    s_busy = 1;
    tal_semaphore_post(s_print_sem);
}

int fortune_printer_busy(void)
{
    return s_busy;
}
