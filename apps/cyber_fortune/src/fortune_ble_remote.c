/**
 * @file fortune_ble_remote.c
 * @brief BLE shake remote receiver — scans for the StickS3 "CF" beacon.
 *
 * Shared contract (sticks3-shake-draw-design §1), manufacturer data (0xFF):
 *
 *   offset  0-1  vendor ID   0xFF 0xFF (test/reserved)
 *           2-3  magic       0x43 0x46 ("CF")
 *           4    version     0x01
 *           5    event type  0x01 = shake
 *           6    sequence    +1 per shake, uint8 rollover, dedup key
 *           7    battery %   reserved, logged only
 *
 * The Stick bursts the beacon at 50ms intervals for 1.5s per shake, so a
 * 50% scan duty cycle catches every burst while leaving air time for the
 * WiFi coexistence. The adv-report callback runs on the BLE stack thread:
 * it only touches flags; the LVGL flow polls fortune_ble_remote_take_event().
 */

#include "tal_api.h"
#include "tal_bluetooth.h"

#include "fortune_ble_remote.h"

#define CF_MFR_PAYLOAD_LEN 8

/***********************************************************
 *********************** static state **********************
 ***********************************************************/
static volatile uint8_t s_pending = 0; /* set by BLE thread, cleared by LVGL */
static uint8_t s_last_seq = 0;         /* BLE thread only */
static uint8_t s_has_last = 0;

/***********************************************************
 ********************** beacon parsing *********************
 ***********************************************************/
static void cf_on_beacon(uint8_t seq, uint8_t batt)
{
    if (s_has_last && seq == s_last_seq) {
        return; /* same burst, already triggered */
    }
    s_has_last = 1;
    s_last_seq = seq;
    s_pending = 1;
    PR_NOTICE("[fortune-ble] shake event seq=%u batt=%u%%", seq, batt);
}

/** Walk the AD structures for manufacturer data carrying the CF magic. */
static void cf_scan_adv(const uint8_t *data, uint8_t len)
{
    uint8_t i = 0;
    while (i + 1 < len) {
        uint8_t ad_len = data[i]; /* covers type byte + payload */
        if (ad_len == 0 || i + 1 + ad_len > len) {
            break;
        }
        if (data[i + 1] == 0xFF && ad_len >= 1 + CF_MFR_PAYLOAD_LEN) {
            const uint8_t *m = &data[i + 2];
            if (m[0] == 0xFF && m[1] == 0xFF &&  /* vendor ID  */
                m[2] == 0x43 && m[3] == 0x46 &&  /* magic "CF" */
                m[4] == 0x01 && m[5] == 0x01) {  /* ver, shake */
                cf_on_beacon(m[6], m[7]);
            }
        }
        i += 1 + ad_len;
    }
}

static void ble_event_cb(TAL_BLE_EVT_PARAMS_T *p_event)
{
    if (p_event->type == TAL_BLE_EVT_ADV_REPORT) {
        TAL_BLE_ADV_REPORT_T *rpt = &p_event->ble_event.adv_report;
        if (rpt->p_data != NULL && rpt->data_len > 0) {
            cf_scan_adv(rpt->p_data, rpt->data_len);
        }
    }
}

/***********************************************************
 ************************ public API ***********************
 ***********************************************************/
int fortune_ble_remote_init(void)
{
    OPERATE_RET rt = tal_ble_bt_init(TAL_BLE_ROLE_CENTRAL, ble_event_cb);
    if (rt != OPRT_OK) {
        PR_ERR("[fortune-ble] bt init failed %d, touch-only mode", rt);
        return -1;
    }

    /* resident passive scan, 50ms window every 100ms (50% duty) */
    TAL_BLE_SCAN_PARAMS_T scan_cfg;
    memset(&scan_cfg, 0, sizeof(scan_cfg));
    scan_cfg.type = TAL_BLE_SCAN_TYPE_PASSIVE;
    scan_cfg.scan_interval = 0xA0; /* 160 * 0.625ms = 100ms */
    scan_cfg.scan_window = 0x50;   /*  80 * 0.625ms =  50ms */
    scan_cfg.timeout = 0;          /* never stop             */
    scan_cfg.filter_dup = 0;       /* dedup by sequence, not by stack */
    rt = tal_ble_scan_start(&scan_cfg);
    if (rt != OPRT_OK) {
        PR_ERR("[fortune-ble] scan start failed %d, touch-only mode", rt);
        return -1;
    }

    PR_NOTICE("[fortune-ble] shake remote scanning");
    return 0;
}

int fortune_ble_remote_take_event(void)
{
    if (s_pending) {
        s_pending = 0;
        return 1;
    }
    return 0;
}
