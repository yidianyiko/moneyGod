/**
 * @file fortune_ble_remote.h
 * @brief BLE shake remote (StickS3 "CF" beacon) receiver.
 *
 * The StickS3 broadcasts a connectionless manufacturer-data beacon on shake
 * (see docs/superpowers/specs/2026-07-25-sticks3-shake-draw-design.md §1).
 * This module scans as BLE central, filters the magic, dedups by event
 * sequence and raises a flag that the LVGL flow polls — no cross-thread
 * UI calls.
 */
#ifndef FORTUNE_BLE_REMOTE_H
#define FORTUNE_BLE_REMOTE_H

#ifdef __cplusplus
extern "C" {
#endif

/** Init BLE central + start the resident scan.
 *  Returns 0 on success, -1 on failure (touch-only degradation). */
int fortune_ble_remote_init(void);

/** Consume a pending shake event. Returns 1 once per event, else 0.
 *  Call from the LVGL task context only. */
int fortune_ble_remote_take_event(void);

#ifdef __cplusplus
}
#endif

#endif /* FORTUNE_BLE_REMOTE_H */
