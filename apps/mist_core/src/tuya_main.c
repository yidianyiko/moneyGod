/**
 * @file tuya_main.c
 * @brief mist_core — T5AI-Core mist co-processor, timed misting firmware.
 *
 * Drives the mist module signal pin (P4, header J2 pin 20) with a
 * periodic pulse pattern: mist bursts for MIST_ON_MS, rests for
 * MIST_OFF_MS, forever.
 *
 * Wiring (T5AI-Core J2 row, last three pins):
 *   module G -> GND (J2 pin 22)
 *   module V -> 5V  (J2 pin 21)
 *   module S -> P4  (J2 pin 20), active-high (vendor: plain LED-style GPIO)
 */

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tkl_output.h"
#include "tkl_gpio.h"

/***********************************************************
 ************************ micro define *********************
 ***********************************************************/
#define MIST_SIG_PIN  TUYA_GPIO_NUM_4 /* J2 pin 20, mist module S wire */
#define MIST_ON_MS    3000            /* burst length */
#define MIST_OFF_MS   5000            /* rest between bursts */

#define TASK_MIST_PRIORITY THREAD_PRIO_2
#define TASK_MIST_SIZE     4096

/***********************************************************
 *********************** variable define *******************
 ***********************************************************/
static THREAD_HANDLE sg_mist_thrd;

/***********************************************************
 *********************** function define *******************
 ***********************************************************/
/**
 * @brief mist pulse task: ON -> OFF loop with logs for the serial monitor
 */
static void __mist_task(void *param)
{
    (void)param;
    uint32_t burst = 0;

    /* signal pin: push-pull output, boot state = OFF (low) */
    TUYA_GPIO_BASE_CFG_T sig_cfg = {
        .mode = TUYA_GPIO_PUSH_PULL,
        .direct = TUYA_GPIO_OUTPUT,
        .level = TUYA_GPIO_LEVEL_LOW,
    };
    tkl_gpio_init(MIST_SIG_PIN, &sig_cfg);
    tkl_gpio_write(MIST_SIG_PIN, TUYA_GPIO_LEVEL_LOW);
    PR_NOTICE("[mist] P4 configured, starting pulse loop (%d ms ON / %d ms OFF)",
              MIST_ON_MS, MIST_OFF_MS);

    while (1) {
        burst++;
        PR_NOTICE("[mist] burst #%u ON", burst);
        tkl_gpio_write(MIST_SIG_PIN, TUYA_GPIO_LEVEL_HIGH);
        tal_system_sleep(MIST_ON_MS);

        PR_NOTICE("[mist] burst #%u OFF", burst);
        tkl_gpio_write(MIST_SIG_PIN, TUYA_GPIO_LEVEL_LOW);
        tal_system_sleep(MIST_OFF_MS);
    }
}

void user_main(void)
{
    OPERATE_RET rt = OPRT_OK;

    /* basic init */
    tal_log_init(TAL_LOG_LEVEL_DEBUG, 1024, (TAL_LOG_OUTPUT_CB)tkl_log_output);

    PR_NOTICE("=== mist_core (T5AI-Core mist co-processor) ===");
    PR_NOTICE("Project name:        %s", PROJECT_NAME);
    PR_NOTICE("App version:         %s", PROJECT_VERSION);
    PR_NOTICE("Compile time:        %s", __DATE__);
    PR_NOTICE("Platform chip:       %s", PLATFORM_CHIP);
    PR_NOTICE("Platform board:      %s", PLATFORM_BOARD);

    static THREAD_CFG_T thrd_param = {0};
    thrd_param.stackDepth = TASK_MIST_SIZE;
    thrd_param.priority = TASK_MIST_PRIORITY;
    thrd_param.thrdname = "mist";
    TUYA_CALL_ERR_LOG(tal_thread_create_and_start(&sg_mist_thrd, NULL, NULL, __mist_task, NULL, &thrd_param));
}

/* Tuya thread handle */
static THREAD_HANDLE ty_app_thread = NULL;

/**
 * @brief app entry thread: run user_main once, then exit
 */
static void tuya_app_thread(void *arg)
{
    (void)arg;
    user_main();

    tal_thread_delete(ty_app_thread);
    ty_app_thread = NULL;
}

void tuya_app_main(void)
{
    THREAD_CFG_T thrd_param = {0};
    thrd_param.stackDepth = 1024 * 4;
    thrd_param.priority = THREAD_PRIO_1;
    thrd_param.thrdname = "tuya_app_main";
    tal_thread_create_and_start(&ty_app_thread, NULL, NULL, tuya_app_thread, NULL, &thrd_param);
}
