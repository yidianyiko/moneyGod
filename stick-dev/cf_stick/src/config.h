#pragma once
/* ---- Shake detection ---- */
#define SHAKE_PEAK_G        2.0f    /* accel magnitude peak threshold (g) */
#define SHAKE_PEAK_COUNT    3       /* peaks required within window */
#define SHAKE_WINDOW_MS     800     /* peak counting window */
#define SHAKE_COOLDOWN_MS   2000    /* cooldown after trigger */
#define IMU_POLL_MS         20      /* 50Hz sampling */
/* ---- BLE beacon ---- */
#define BLE_BURST_MS        1500    /* advertising burst duration */
#define BLE_ADV_INTERVAL    0x50    /* 50ms (0.625ms units) */
/* ---- Audio ---- */
#define SPK_VOL_USB         200     /* speaker volume on USB power (0-255) */
#define SPK_VOL_BATT        180     /* volume on battery, keep <75% to avoid brownout */
