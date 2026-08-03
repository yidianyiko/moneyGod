/**
 * @file tuya_main.c
 * @brief Cyber Fortune Temple (赛博财神庙) — application entry.
 *
 * Boots the board, brings up LVGL on the 3.5" ILI9488 + GT1151 touch panel,
 * builds the fortune-stick UI, then starts the LVGL task loop.
 */

#include "tuya_cloud_types.h"

#include "tal_api.h"
#include "tkl_output.h"
#include "tkl_system.h"
#include "tkl_gpio.h"

#include "lvgl.h"
#include "lv_vendor.h"
#include "board_com_api.h"

#include "fortune_flow.h"
#include "fortune_net.h"
#include "fortune_printer.h"
#include "fortune_ble_remote.h"
#include "fortune_audio.h"

/* Beken USB driver entry (platform bk_usb, CherryUSB host stack). */
extern int bk_usb_open(unsigned int usb_mode); /* 0 = USB_HOST_MODE */

/***********************************************************
 *********************** function define *******************
 ***********************************************************/
/**
 * @brief Power the USB-A host port and start the CherryUSB host stack.
 *
 * GPIO28 drives the on-board USB VBUS switch (same recipe as the Tuya
 * factory test in tkl_mftest.c). Once the stack is up, the EM5820H printer
 * plugged into the USB-A port is picked up by the bk_usbh_printer class
 * driver automatically.
 */
static void usb_host_probe_start(void)
{
    TUYA_GPIO_BASE_CFG_T cfg;
    cfg.mode = TUYA_GPIO_PULLUP;
    cfg.direct = TUYA_GPIO_OUTPUT;
    cfg.level = TUYA_GPIO_LEVEL_HIGH;
    tkl_gpio_init(TUYA_GPIO_NUM_28, &cfg);
    tkl_gpio_write(TUYA_GPIO_NUM_28, TUYA_GPIO_LEVEL_HIGH);

    int ret = bk_usb_open(0);
    PR_NOTICE("[usb-probe] VBUS on (GPIO28), bk_usb_open(0) ret=%d", ret);
}

void user_main(void)
{
    /* basic init */
    tal_log_init(TAL_LOG_LEVEL_DEBUG, 4096, (TAL_LOG_OUTPUT_CB)tkl_log_output);

    PR_NOTICE("=== Cyber Fortune Temple (赛博财神庙) ===");
    PR_NOTICE("Project name:        %s", PROJECT_NAME);
    PR_NOTICE("App version:         %s", PROJECT_VERSION);
    PR_NOTICE("Compile time:        %s", __DATE__);
    PR_NOTICE("TuyaOpen version:    %s", OPEN_VERSION);
    PR_NOTICE("Platform chip:       %s", PLATFORM_CHIP);
    PR_NOTICE("Platform board:      %s", PLATFORM_BOARD);

    /* register board hardware (display + touch + audio codec) */
    board_register_hardware();

    /* speaker smoke test: boot chime through the JST speaker socket */
    if (fortune_audio_init() == 0) {
        fortune_audio_chime();
    }

    /* WiFi + backend request worker (needs kv/timer/workq/lwip/netmgr) */
    fortune_net_init();

    /* BLE shake remote (StickS3 beacon); failure degrades to touch-only */
    fortune_ble_remote_init();

    /* USB host up, then the printer worker (EM5820H on the USB-A port) */
    usb_host_probe_start();
    fortune_printer_init();

    /* bring up LVGL bound to the registered display */
    lv_vendor_init(DISPLAY_NAME);

    /* build the UI under the display lock, then start the LVGL task */
    lv_vendor_disp_lock();
    fortune_flow_start();
    lv_vendor_disp_unlock();

    lv_vendor_start(5, 1024 * 8);
}

/***********************************************************
 *********************** entry point ***********************
 ***********************************************************/
#if OPERATING_SYSTEM == SYSTEM_LINUX
void main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    user_main();

    while (1) {
        tal_system_sleep(500);
    }
}
#else

static THREAD_HANDLE ty_app_thread = NULL;

static void tuya_app_thread(void *arg)
{
    (void)arg;
    user_main();

    tal_thread_delete(ty_app_thread);
    ty_app_thread = NULL;
}

void tuya_app_main(void)
{
    THREAD_CFG_T thrd_param;

    memset(&thrd_param, 0, sizeof(THREAD_CFG_T));
    thrd_param.stackDepth = 1024 * 4;
    thrd_param.priority   = THREAD_PRIO_1;
    thrd_param.thrdname   = "tuya_app_main";

    tal_thread_create_and_start(&ty_app_thread, NULL, NULL, tuya_app_thread, NULL, &thrd_param);
}
#endif
