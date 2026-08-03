/**
 * @file fortune_printer.h
 * @brief Cyber Fortune Temple — USB thermal printer ticket output.
 */
#ifndef FORTUNE_PRINTER_H
#define FORTUNE_PRINTER_H

#include "fortune_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the background print worker thread. Call once at boot,
 *        after the USB host stack is up.
 */
void fortune_printer_init(void);

/**
 * @brief Queue a fortune result for printing (non-blocking, safe to call
 *        from the LVGL task; the result is copied internally).
 *        Silently skipped if no printer is attached.
 */
void fortune_printer_print(const fortune_result_t *result);

/**
 * @brief Same as fortune_printer_print() but appends the 解签 Q&A section
 *        when @p extra carries an interpretation. @p extra may be NULL.
 */
void fortune_printer_print_full(const fortune_result_t *result, const fortune_extra_t *extra);

/** True while the worker is building/sending a ticket. */
int fortune_printer_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* FORTUNE_PRINTER_H */
