# Субагент 1: Модуль локализации (Translation)

**Задача:** Реализовать ядро i18n — загрузка JSON-локалей и API получения строк по ключу.

**Сделано:**
- Добавлены `source/text/Translation.h` и `source/text/Translation.cpp`.
- API: `Translation::Load(languageCode)`, `Translation::SetLanguage(code)`, `Translation::Tr(key)`, `Translation::Tr(key, replacements)` для подстановок `{{name}}`.
- Минимальный парсер JSON (без внешних зависимостей): плоский объект "key": "value", поддержка `\uXXXX` в значениях, хранение в `std::map<std::string, std::string>`.
- При отсутствии ключа — однократная подгрузка fallback-локали `en`, затем возврат ключа как строки.
- Путь к файлам: `Files::Resources() / "lang" / (code + ".json")` (в аг.2 путь изменён на `Files::Data() / "lang"`).
- В `GameData::LoadSettings()` добавлен вызов инициализации локали.
- В `source/CMakeLists.txt` добавлены `text/Translation.cpp` и `text/Translation.h`.

**Файлы:** Translation.h (новый), Translation.cpp (новый), source/CMakeLists.txt, GameData.cpp.
