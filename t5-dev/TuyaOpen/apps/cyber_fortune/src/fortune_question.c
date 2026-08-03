/**
 * @file fortune_question.c
 * @brief Local follow-up question bank — one colloquial question per draw.
 *
 * Questions and options are plain UTF-8 const data; the picked option text
 * is sent to the backend together with the category so the LLM can write a
 * targeted fortune. All strings stay inside GB2312 so both the px24 font
 * and the GBK printer table cover them.
 */

#include "fortune_question.h"

#include "tal_api.h"

#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))

const char *const fortune_category_names[FORTUNE_CAT_COUNT] = {
    "财运", "事业", "姻缘", "学业", "健康", "今日运势",
};

/* --- 财运 --- */
static const fortune_question_t QUESTIONS_WEALTH[] = {
    {"最近让你惦记的钱，是哪一种？",
     {"工资和奖金", "投资和理财", "副业外快", "意外之财"}, 4},
    {"你现在的钱包状态更像？",
     {"稳稳当当", "紧巴巴的", "起起伏伏"}, 3},
    {"如果财神爷给你一笔钱，你会？",
     {"先存起来", "投出去钱生钱", "犒劳自己", "还债上岸"}, 4},
};

/* --- 事业 --- */
static const fortune_question_t QUESTIONS_CAREER[] = {
    {"眼下工作里最挂心的事是？",
     {"升职加薪", "跳槽换方向", "项目成败", "同事关系"}, 4},
    {"你现在的干劲儿属于？",
     {"火力全开", "按部就班", "有点倦了"}, 3},
    {"未来一年你最想要的是？",
     {"更大的舞台", "更稳的位子", "自己当老板"}, 3},
};

/* --- 姻缘 --- */
static const fortune_question_t QUESTIONS_LOVE[] = {
    {"你现在的感情状态是？",
     {"单身待缘", "暧昧不明", "热恋之中", "老夫老妻"}, 4},
    {"关于感情，你最想问的是？",
     {"正缘何时来", "他心里有我吗", "这段感情长久吗"}, 3},
    {"遇到心动的人，你通常会？",
     {"主动出击", "默默守候", "顺其自然"}, 3},
};

/* --- 学业 --- */
static const fortune_question_t QUESTIONS_STUDY[] = {
    {"最近的学习目标是？",
     {"大考在即", "考证考级", "留学深造", "自我提升"}, 4},
    {"你现在的学习状态是？",
     {"劲头十足", "时松时紧", "心不在焉"}, 3},
    {"学习上最让你发愁的是？",
     {"时间不够用", "效率上不去", "心里没底"}, 3},
};

/* --- 健康 --- */
static const fortune_question_t QUESTIONS_HEALTH[] = {
    {"最近身体最常提醒你的是？",
     {"睡不好", "肩颈腰背", "肠胃闹脾气", "没什么大事"}, 4},
    {"你平时的作息属于？",
     {"早睡早起", "熬夜大户", "看心情"}, 3},
    {"关于健康你最想要的是？",
     {"精力更旺", "身材管理", "少生病"}, 3},
};

/* --- 今日运势 --- */
static const fortune_question_t QUESTIONS_TODAY[] = {
    {"今天你最希望顺利的是？",
     {"工作学习", "出行办事", "人际社交", "心情状态"}, 4},
    {"今天醒来的第一感觉是？",
     {"元气满满", "平平常常", "有点没睡醒"}, 3},
    {"今天你打算怎么过？",
     {"大干一场", "稳稳当当", "躺平休息"}, 3},
};

typedef struct {
    const fortune_question_t *items;
    int count;
} question_set_t;

static const question_set_t QUESTION_SETS[FORTUNE_CAT_COUNT] = {
    {QUESTIONS_WEALTH, ARRAY_LEN(QUESTIONS_WEALTH)},
    {QUESTIONS_CAREER, ARRAY_LEN(QUESTIONS_CAREER)},
    {QUESTIONS_LOVE,   ARRAY_LEN(QUESTIONS_LOVE)},
    {QUESTIONS_STUDY,  ARRAY_LEN(QUESTIONS_STUDY)},
    {QUESTIONS_HEALTH, ARRAY_LEN(QUESTIONS_HEALTH)},
    {QUESTIONS_TODAY,  ARRAY_LEN(QUESTIONS_TODAY)},
};

const fortune_question_t *fortune_question_pick(fortune_category_t cat)
{
    if (cat >= FORTUNE_CAT_COUNT) {
        cat = FORTUNE_CAT_TODAY;
    }
    const question_set_t *set = &QUESTION_SETS[cat];
    int idx = tal_system_get_random(set->count);
    if (idx < 0 || idx >= set->count) {
        idx = 0;
    }
    return &set->items[idx];
}
