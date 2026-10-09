import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FONT_PATH = ROOT / "assets/fonts/potion_font_16.c"
INVENTORY_PATH = ROOT / "assets/fonts/potion_ui_zh_CN.txt"


def non_ascii_characters(text: str) -> set[str]:
    return {character for character in text if ord(character) > 0x7F}


def main() -> None:
    application_text = (ROOT / "main/main.c").read_text(encoding="utf-8")
    model_text = (ROOT / "main/potion_model.c").read_text(encoding="utf-8")
    inventory_text = INVENTORY_PATH.read_text(encoding="utf-8")
    font_text = FONT_PATH.read_text(encoding="utf-8")

    required = non_ascii_characters(application_text + model_text)
    inventory = non_ascii_characters(inventory_text)
    encoded = {chr(int(value, 16)) for value in re.findall(r"/\* U\+([0-9A-F]+)", font_text)}

    missing_inventory = required - inventory
    missing_font = inventory - encoded
    assert not missing_inventory, f"Chinese UI inventory is missing: {sorted(missing_inventory)}"
    assert not missing_font, f"generated potion font is missing: {sorted(missing_font)}"
    assert ".range_start = 32, .range_length = 95" in font_text, "printable ASCII range is missing"
    assert "LV_FONT_DECLARE(potion_font_16);" in application_text
    assert "&potion_font_16" in application_text
    assert "lv_font_montserrat" not in application_text

    print(f"potion_font_16: {len(required)} required Chinese glyphs covered")


if __name__ == "__main__":
    main()
