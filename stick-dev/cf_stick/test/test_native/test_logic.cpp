#include <unity.h>
#include "shake_detector.h"
#include "ble_beacon.h"

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

/* Protocol contract test, spec section 1: manufacturer data layout */
void test_beacon_payload(void) {
    uint8_t buf[8];
    beacon_build_payload(buf, /*event=*/0x01, /*seq=*/42, /*batt=*/88);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[0]);  /* vendor ID lo */
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[1]);  /* vendor ID hi */
    TEST_ASSERT_EQUAL_HEX8(0x43, buf[2]);  /* 'C' */
    TEST_ASSERT_EQUAL_HEX8(0x46, buf[3]);  /* 'F' */
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[4]);  /* protocol version */
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[5]);  /* event type */
    TEST_ASSERT_EQUAL_HEX8(42,   buf[6]);  /* sequence */
    TEST_ASSERT_EQUAL_HEX8(88,   buf[7]);  /* battery % */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_shake_triggers_once);
    RUN_TEST(test_slow_peaks_no_trigger);
    RUN_TEST(test_beacon_payload);
    return UNITY_END();
}
