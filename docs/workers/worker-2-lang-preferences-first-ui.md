# Субагент 2: Lang-файлы, настройки языка, первый блок UI

**Задача:** Создать данные локалей, поддержку языка в настройках и заменить литералы в Preferences, PreferencesPanel, BankPanel.

**Сделано:**
- Создан `data/lang/en.json` — плоский UTF-8 JSON с ключами из ТЗ (prefs.*, bank.*, gamerules.*, message.*, date.*) и расширенным набором для настроек и банка.
- В корневом CMakeLists.txt добавлены комментарии к `install(DIRECTORY data ...)` — установка уже включает `data/lang`.
- В Preferences добавлены `Language()` и `SetLanguage(code)`; чтение/запись ключа `language` в preferences.txt, по умолчанию `"en"`.
- В `GameData::LoadSettings()` вызывается `Translation::SetLanguage(Preferences::Language())`; путь загрузки локалей в Translation.cpp изменён на `Files::Data() / "lang"`.
- Замена литералов на `Translation::Tr("key")` в Preferences.cpp, PreferencesPanel.cpp, BankPanel.cpp; все отображаемые строки настроек и банка переведены на ключи.

**Файлы:** data/lang/en.json (новый), CMakeLists.txt, Preferences.h, Preferences.cpp, PreferencesPanel.cpp, BankPanel.cpp, GameData.cpp, source/text/Translation.cpp.
