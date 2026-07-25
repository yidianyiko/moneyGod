#include <M5Unified.h>
#include "config.h"
#include "shake_detector.h"
#include "ble_beacon.h"
#include "ui_anim.h"
#include "assets/wav_shake.h"

static ShakeDetector s_shake;
static uint8_t s_seq = 0;
static uint32_t s_burst_until = 0;

static void apply_volume(void) {
    bool usb = M5.Power.isCharging() || M5.Power.getBatteryLevel() < 0;
    M5.Speaker.setVolume(usb ? SPK_VOL_USB : SPK_VOL_BATT);
}

void setup() {
    auto cfg = M5.config();
    cfg.internal_imu = true;
    cfg.internal_spk = true;
    M5.begin(cfg);
    M5.Display.setRotation(1);           /* landscape 240x135 */
    Serial.begin(115200);
    beacon_init();
    ui_init();
    apply_volume();
    Serial.println("[cf-stick] ready");
}

void loop() {
    M5.update();
    uint32_t now = millis();

    /* IMU polling at 50Hz */
    static uint32_t last_poll = 0;
    if (now - last_poll >= IMU_POLL_MS) {
        last_poll = now;
        float ax, ay, az;
        if (M5.Imu.getAccel(&ax, &ay, &az)) {
            if (s_shake.update(ax, ay, az, now)) {
                s_seq++;
                uint8_t batt = (uint8_t)M5.Power.getBatteryLevel();
                Serial.printf("[shake] seq=%u batt=%u%%\n", s_seq, batt);
                beacon_burst_start(s_seq, batt);      /* 1. broadcast */
                s_burst_until = now + BLE_BURST_MS;
                apply_volume();                       /* 2. sound */
                M5.Speaker.playWav(wav_shake, wav_shake_len);
                ui_play_shake();                      /* 3. animation */
            }
        }
    }
    /* stop the advertising burst on schedule */
    if (s_burst_until && now > s_burst_until) {
        s_burst_until = 0;
        beacon_stop();
    }
    ui_tick();
    delay(2);
}
