#include <M5Unified.h>
#include "config.h"
#include "shake_detector.h"
#include "ble_beacon.h"

static ShakeDetector s_shake;
static uint8_t s_seq = 0;
static uint32_t s_burst_until = 0;

void setup() {
    auto cfg = M5.config();
    cfg.internal_imu = true;
    M5.begin(cfg);
    M5.Display.setRotation(1);           /* landscape 240x135 */
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_GOLD);
    M5.Display.setTextSize(2);
    M5.Display.drawString("shake me", 10, 10);
    Serial.begin(115200);
    beacon_init();
    M5.Display.drawString("BLE ready", 10, 40);
}

void loop() {
    M5.update();
    static uint32_t last_poll = 0;
    uint32_t now = millis();

    if (now - last_poll >= IMU_POLL_MS) {
        last_poll = now;
        float ax, ay, az;
        if (M5.Imu.getAccel(&ax, &ay, &az)) {
            if (s_shake.update(ax, ay, az, now)) {
                s_seq++;
                uint8_t batt = (uint8_t)M5.Power.getBatteryLevel();
                Serial.printf("[shake] fired! seq=%u batt=%u%%\n", s_seq, batt);
                beacon_burst_start(s_seq, batt);
                s_burst_until = now + BLE_BURST_MS;
                /* on-screen debug: seq + battery + burst state */
                M5.Display.fillRect(0, 60, 240, 40, TFT_BLACK);
                M5.Display.setCursor(10, 60);
                M5.Display.printf("seq=%u batt=%u%%", s_seq, batt);
                M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
                M5.Display.drawString("ADV ON ", 10, 80);
                M5.Display.setTextColor(TFT_GOLD, TFT_BLACK);
            }
        }
    }
    if (s_burst_until && now > s_burst_until) {
        s_burst_until = 0;
        beacon_stop();
        Serial.println("[beacon] burst end");
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.drawString("ADV OFF", 10, 80);
        M5.Display.setTextColor(TFT_GOLD, TFT_BLACK);
    }
    delay(2);
}
