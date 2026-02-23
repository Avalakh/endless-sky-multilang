Вводные данные:
Измененные исходники с перевеводом игры на мультиязычную систему "endless-sky". 
Оригинальные исходники "endless-sky-master".

Задача: 
Подготовь скрипт и cmd для его запуска с функционалом переделать оригинальные исходники в измененные, с учетом возможных изменений(обновлений) оригинальных файлов. Помести скрипт в папку tools

Короткая инструкция запуска:
1) Обновить пакет изменений (bundle): `tools\sync_multilang_changes.cmd --create-bundle`
2) Применить изменения к оригиналу в отдельную папку:
`tools\sync_multilang_changes.cmd --target-root "D:\Work\projects\endless-sky-master" --output-root "D:\Work\projects\endless-sky-patched" --clean-output`
3) Применить изменения прямо в target (по желанию): `tools\sync_multilang_changes.cmd --target-root "D:\Work\projects\endless-sky-master" --in-place`