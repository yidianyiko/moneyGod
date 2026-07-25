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
    /* boot audio self-test: show speaker state, beep, then play the chime */
    M5.Display.fillScreen(TFT_WHITE);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(8, 8);
    M5.Display.printf("SPK %s vol %u", M5.Speaker.isEnabled() ? "OK" : "OFF",
                      (unsigned)M5.Speaker.getVolume());
    M5.Speaker.tone(1000, 1500);
    delay(300);                          /* let the enable callback run */
    /* audio chain diagnostic: codec + PMIC presence, PA enable bit */
    bool es_ok = M5.In_I2C.scanID(0x18); /* ES8311 codec */
    bool pm_ok = M5.In_I2C.scanID(0x6E); /* M5 PM1 PMIC */
    uint8_t pm11 = M5.In_I2C.readRegister8(0x6E, 0x11, 100000);
    uint8_t es13 = M5.In_I2C.readRegister8(0x18, 0x13, 100000);
    M5.Display.setCursor(8, 32);
    M5.Display.printf("ES:%c PM:%c", es_ok ? 'Y' : 'N', pm_ok ? 'Y' : 'N');
    M5.Display.setCursor(8, 56);
    M5.Display.printf("PM11=%02X ES13=%02X", pm11, es13);
    delay(1500);
    M5.Speaker.playWav(wav_shake, wav_shake_len);
    delay(1500);
    ui_play_idle();
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
