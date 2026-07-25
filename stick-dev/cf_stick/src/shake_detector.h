#pragma once
#include <stdint.h>
#include <math.h>
#include "config.h"

/* Peak + window counting shake detector. A peak is a rising edge of the
 * accel magnitude above threshold (with fall-back re-arm hysteresis). */
class ShakeDetector {
public:
    /* ax/ay/az in g; now_ms monotonic milliseconds. Returns true = fire once. */
    bool update(float ax, float ay, float az, uint32_t now_ms) {
        if (now_ms - _last_fire < SHAKE_COOLDOWN_MS && _last_fire != 0) return false;
        float mag = sqrtf(ax * ax + ay * ay + az * az);
        bool above = mag > SHAKE_PEAK_G;
        if (above && !_armed) {
            _armed = true;
            /* window expired: start a new one */
            if (_count == 0 || now_ms - _window_start > SHAKE_WINDOW_MS) {
                _window_start = now_ms;
                _count = 0;
            }
            _count++;
            if (_count >= SHAKE_PEAK_COUNT && now_ms - _window_start <= SHAKE_WINDOW_MS) {
                _count = 0;
                _last_fire = now_ms ? now_ms : 1;
                return true;
            }
        } else if (!above && mag < SHAKE_PEAK_G * 0.7f) {
            _armed = false;   /* fell back below hysteresis: re-arm peak detection */
        }
        return false;
    }
private:
    bool _armed = false;
    uint8_t _count = 0;
    uint32_t _window_start = 0;
    uint32_t _last_fire = 0;
};
