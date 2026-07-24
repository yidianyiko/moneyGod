"""验证火山引擎 Ark 连通性。运行: python quickstart.py"""
from src.config import SEED_MODEL
from src.llm import ask


def main() -> None:
    text = ask("请用一句话确认你能正常回复。", model=SEED_MODEL)
    print("Ark 返回:", text)


if __name__ == "__main__":
    main()
