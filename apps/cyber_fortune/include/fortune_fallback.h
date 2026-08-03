/**
 * @file fortune_fallback.h
 * @brief Local fallback lots — used when the backend is unreachable so the
 *        draw always succeeds (seamless degradation, user never notices).
 */
#ifndef FORTUNE_FALLBACK_H
#define FORTUNE_FALLBACK_H

#include "fortune_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fill @p out with a locally generated lot for the category:
 *        weighted random grade (same 15/30/35/15/5 weights as the backend),
 *        random lot number and a matching pre-written text.
 */
void fortune_fallback_get(fortune_category_t cat, fortune_result_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FORTUNE_FALLBACK_H */
