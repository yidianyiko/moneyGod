"""fortune 模块单测:抽档分布 / 兜底结构 / LLM 成功与失败路径。"""
from unittest.mock import patch

from src.fortune import (
    CATEGORIES,
    FALLBACK,
    GRADES,
    draw_grade,
    generate_fortune,
)

GRADE_NAMES = {name for name, _, _ in GRADES}
RESULT_KEYS = {"lot_no", "grade", "grade_score", "poem", "explanation", "advice"}


def test_draw_grade_distribution():
    seen = set()
    for _ in range(1000):
        name, score = draw_grade()
        assert name in GRADE_NAMES
        assert 1 <= score <= 5
        seen.add(name)
    # 1000 次抽样,15%/30%/35% 的档位必然出现
    assert {"上上", "上", "中"} <= seen


def test_fallback_structure_complete():
    assert set(FALLBACK.keys()) == set(CATEGORIES)
    for cat in CATEGORIES:
        assert set(FALLBACK[cat].keys()) == GRADE_NAMES
        for grade in GRADE_NAMES:
            entry = FALLBACK[cat][grade]
            assert len(entry["poem"]) == 4
            assert entry["explanation"]
            assert entry["advice"]


def _check_result(result):
    assert set(result.keys()) == RESULT_KEYS
    assert 1 <= result["lot_no"] <= 100
    assert result["grade"] in GRADE_NAMES
    assert 1 <= result["grade_score"] <= 5
    assert len(result["poem"]) == 4
    assert all(isinstance(p, str) and p for p in result["poem"])
    assert result["explanation"]
    assert result["advice"]


def test_generate_fortune_llm_ok():
    fake = {
        "poem": ["代码跑通钱包鼓", "老板看你像宝物", "别急着买大玩具", "先把本金存三成"],
        "explanation": "你纠结的换工作这件事,眼下时机偏好,新东家诚意足,谈薪时别客气。",
        "advice": "本周内更新简历,约两个内推聊聊行情。",
    }
    with patch("src.fortune.ask_json", return_value=fake) as mocked:
        result = generate_fortune("财运", "最近在纠结什么", "要不要换工作")
    mocked.assert_called_once()
    _check_result(result)
    assert result["poem"] == fake["poem"]


def test_generate_fortune_llm_bad_fields_falls_back():
    with patch("src.fortune.ask_json", return_value={"poem": ["只有一句"]}):
        result = generate_fortune("事业", "问题", "回答")
    _check_result(result)
    # 兜底文案必然来自 FALLBACK
    assert result["poem"] == FALLBACK["事业"][result["grade"]]["poem"]


def test_generate_fortune_llm_raises_falls_back():
    with patch("src.fortune.ask_json", side_effect=RuntimeError("Ark 调用失败")):
        result = generate_fortune("姻缘", "问题", "回答")
    _check_result(result)


def test_generate_fortune_unknown_category_falls_back_to_default():
    with patch("src.fortune.ask_json", side_effect=RuntimeError("boom")):
        result = generate_fortune("神秘类目", "问题", "回答")
    _check_result(result)
    assert result["poem"] == FALLBACK["今日运势"][result["grade"]]["poem"]


def test_generate_fortune_sanitizes_rare_chars():
    fake = {
        "poem": ["好事排队到门口𠮷", "你只管往前走", "风向水向都帮你", "记得说声谢谢"],
        "explanation": "一切顺利𠮷。",
        "advice": "去做就好。",
    }
    with patch("src.fortune.ask_json", return_value=fake):
        result = generate_fortune("财运", "q", "a")
    # GBK 编不出的字符被剔除
    assert "𠮷" not in result["poem"][0]
    assert "𠮷" not in result["explanation"]
