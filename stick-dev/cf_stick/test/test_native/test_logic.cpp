#include <unity.h>
#include "shake_detector.h"

/* Simulate 50Hz sampling: rest -> 3 violent shakes -> should fire once,
 * further shaking within cooldown must not fire again. */
void test_shake_triggers_once(void) {
    ShakeDetector det;
    uint32_t ms = 0;
    bool fired = false;
    /* at rest, 1g */
    for (int i = 0; i < 50; i++) { fired |= det.update(0, 0, 1.0f, ms); ms += 20; }
    TEST_ASSERT_FALSE(fired);
    /* 3 peaks of 2.5g, 200ms apart (inside the 800ms window) */
    for (int p = 0; p < 3; p++) {
        fired |= det.update(2.5f, 0, 0, ms); ms += 20;
        for (int i = 0; i < 9; i++) { fired |= det.update(0, 0, 1.0f, ms); ms += 20; }
    }
    TEST_ASSERT_TRUE(fired);
    /* keep shaking within cooldown: no re-trigger */
    bool again = false;
    for (int p = 0; p < 3; p++) {
        again |= det.update(2.5f, 0, 0, ms); ms += 20;
        for (int i = 0; i < 9; i++) { again |= det.update(0, 0, 1.0f, ms); ms += 20; }
    }
    TEST_ASSERT_FALSE(again);
}

void test_slow_peaks_no_trigger(void) {
    ShakeDetector det;
    uint32_t ms = 0;
    bool fired = false;
    /* 3 peaks but 1s apart, outside the 800ms window: no trigger */
    for (int p = 0; p < 3; p++) {
        fired |= det.update(2.5f, 0, 0, ms); ms += 20;
        for (int i = 0; i < 49; i++) { fired |= det.update(0, 0, 1.0f, ms); ms += 20; }
    }
    TEST_ASSERT_FALSE(fired);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_shake_triggers_once);
    RUN_TEST(test_slow_peaks_no_trigger);
    return UNITY_END();
}
