#include <M5Unified.h>
#include "config.h"

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);           /* landscape 240x135 */
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_GOLD);
    M5.Display.setTextSize(2);
    M5.Display.drawString("CF Stick OK", 10, 10);
    Serial.begin(115200);
}

void loop() {
    M5.update();
    static uint32_t last = 0;
    if (millis() - last > 1000) {
        last = millis();
        Serial.printf("[hb] up=%lus batt=%d%%\n", millis() / 1000, M5.Power.getBatteryLevel());
    }
    delay(10);
}
