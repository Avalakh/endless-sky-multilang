# Субагент 4: Шрифт и UTF-8 (кодовые точки)

**Задача:** Интерпретировать строки в Font как UTF-8 и вести обход по кодовым точкам вместо байтов.

**Сделано:**
- В `Font.h`: объявлен приватный метод `GlyphForCodepoint(uint32_t codepoint, bool isAfterSpace)`; сигнатура `WidthRawString` изменена на `(const std::string &str, char after)` для итерации по UTF-8.
- В `Font.cpp`: подключён `Utf8.h`. Реализован `GlyphForCodepoint`: ASCII 32–126 — прежняя логика (индекс глифа); кавычки U+2018/U+201C после пробела — 96/97; остальные кодовые точки и невалидный UTF-8 (0xFFFFFFFF) — fallback-глиф 0.
- В `DrawAliased(const std::string &str, ...)` и `WidthRawString` цикл по байтам заменён на итерацию по кодовым точкам через `Utf8::DecodeCodePoint`; для каждой точки вызывается `GlyphForCodepoint`, подсчёт ширины и отрисовка по глифам без изменения размера атласа (GLYPHS=98).
- Атлас и кернинг не менялись; добавление кириллицы — отдельный этап.

**Файлы:** source/text/Font.h, source/text/Font.cpp.
