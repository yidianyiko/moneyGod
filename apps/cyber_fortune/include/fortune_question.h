/**
 * @file fortune_question.h
 * @brief Local pre-written follow-up question bank (one question per draw).
 */
#ifndef FORTUNE_QUESTION_H
#define FORTUNE_QUESTION_H

#include "fortune_data.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FORTUNE_Q_MAX_OPTS 4

typedef struct {
    const char *text;                        /* question shown to the user */
    const char *options[FORTUNE_Q_MAX_OPTS]; /* NULL-terminated if < 4     */
    int option_count;
} fortune_question_t;

/** Randomly pick one question for the category (never returns NULL). */
const fortune_question_t *fortune_question_pick(fortune_category_t cat);

#ifdef __cplusplus
}
#endif

#endif /* FORTUNE_QUESTION_H */
