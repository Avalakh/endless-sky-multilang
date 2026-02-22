# Субагент 3: Замена литералов в остальных панелях

**Задача:** Вынести пользовательские строки в ключи и заменить литералы на `Tr("key")` в оставшихся файлах из ТЗ.

**Сделано:**
- Во всех перечисленных файлах добавлен `#include "text/Translation.h"`, пользовательские строки заменены на `Translation::Tr("key")` или `Tr("key", map)` для плейсхолдеров.
- Обработаны: GamerulesPanel.cpp (gamerules.*), Date.cpp (date.*), ConversationPanel.cpp (dialog.*), Engine.cpp, AI.cpp (message.*), MainPanel.cpp, MissionPanel.cpp, LoadPanel.cpp (ui.*, dialog.*), HailPanel.cpp (hail.*, message.*), ItemInfoDisplay.cpp, OutfitterPanel.cpp, ShipInfoDisplay.cpp, OutfitInfoDisplay.cpp (ui.*, outfit.*).
- В `data/lang/en.json` добавлены ключи: gamerules.*, date.*, dialog.*, message.*, mission.*, ui.*, hail.*, outfit.* с английскими значениями и плейсхолдерами `{{name}}`, `{{count}}` и т.д.
- PlanetPanel.cpp не изменён — пользовательских литералов не найдено.

**Файлы:** GamerulesPanel, Date, ConversationPanel, Engine, AI, MainPanel, MissionPanel, LoadPanel, HailPanel, ItemInfoDisplay, OutfitterPanel, ShipInfoDisplay, OutfitInfoDisplay, data/lang/en.json.
