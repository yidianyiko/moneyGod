#pragma once
#include <stdint.h>
#include <string.h>

#define BEACON_PAYLOAD_LEN 8

/* Pure function: build manufacturer data per spec section 1 */
inline void beacon_build_payload(uint8_t out[BEACON_PAYLOAD_LEN],
                                 uint8_t event, uint8_t seq, uint8_t batt) {
    out[0] = 0xFF; out[1] = 0xFF;          /* vendor ID (test/reserved value) */
    out[2] = 0x43; out[3] = 0x46;          /* magic "CF" */
    out[4] = 0x01;                         /* protocol version */
    out[5] = event;                        /* event type */
    out[6] = seq;                          /* event sequence */
    out[7] = batt;                         /* battery % */
}

#ifdef ARDUINO
void beacon_init(void);                              /* BLEDevice init */
void beacon_burst_start(uint8_t seq, uint8_t batt);  /* set payload and start advertising */
void beacon_stop(void);                              /* stop advertising */
#endif
