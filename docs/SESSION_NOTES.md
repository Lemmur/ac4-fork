# SESSION_NOTES — журнал рабочих сессий RuDub Studio (форк Audacity 4)

## Сессия 2026-09-14 — ШАГ 1: АНАЛИЗ кодовой базы

**Сделано:**
- Прочитан AGENTS.md, порядок работы (АНАЛИЗ → АРХИТЕКТУРА → ПЛАН → код) зафиксирован.
- Изучена реальная структура репозитория: три слоя — `src/` (Au4, Qt6 QML), `muse/framework` (каркас MuseScore), `au3/libraries` + `au3/modules/import-export` (ядро Au3 без GUI). `au3/src` (легаси wx-приложение) в сборку AU4 НЕ входит.
- Создан `docs/analysis/audacity4_map.md` — полная карта: формат проекта, модель клипов, UI/докинг, аудио/ASIO, эффекты, импорт, команды/макросы, лицензии, сборка + итоговые таблицы требования→код.

**Ключевые установленные факты (кратко):**
1. `.aup3` = SQLite, сохранён без изменений. Точка расширения для данных дубляжа — `ProjectFileIOExtension` (au3-project-file-io).
2. Клипы: au3 `WaveTrack`/`WaveClip`; из кода Au4 ссылка = `TrackId`+`ClipId` (`trackedit/trackedittypes.h`) через `au3wrap/internal/domaccessor.h`.
3. UI: Qt 6 QML; докинг — `muse/framework/dockwindow` (форк KDDockWidgets, GPL-3.0); панели регистрируются в `src/appshell/.../ProjectPage.qml`; модули приложения — `src/app/appfactory.cpp`.
4. Аудио: PortAudio 19.7.0 + ASIO SDK 2.3.4 автоматически из `muse_deps` (`PA_USE_ASIO=ON`), ничего доустанавливать не нужно. ASIO-UI уже есть (AsioSection.qml). Мониторинг/мейлы управляются настройками AudioIO.
5. Эффекты: ядро = класс au3-стиля (наследник Stateful(less)PerTrackEffect) + `BuiltinEffectsModule::Registration<T>`; минимальные образцы — `builtin_collection/fade` (без UI) и `amplify` (с QML). Регистрации — `builtincollectionloader.cpp::preInit()`.
6. Импорт: `Au3Importer::importIntoTrack(path, trackId, startTime)` пригоден как блок; массового импорта по guid нет — писать свой сервис.
7. CommandManager/Macros (BatchCommands) в Au4 НЕ собираются; актуальная система — muse actions/shortcuts. Движок пакетной обработки — с нуля.
8. Лицензии: GPLv3 (корень), muse GPLv3+CLA, KDDockWidgets GPL-3.0, ASIO SDK — проприетарная Steinberg (бинарники с ASIO не распространять публично).
9. Сборка: C++20 (не C++17!), CMake≥3.24, Ninja, MSVC 2022 x64, Qt 6.10 (+5 Compatibility, Network Auth, Shader Tools, State Machines); пресеты `audacity-debug/asan/release`; тесты — ctest (AU_BUILD_*_TESTS).
10. Автосейв: включён по умолчанию, 5 минут, только при изменениях (`projectautosaver.cpp`, `projectconfiguration.cpp`) — совпадает с требованием 6.7.
11. Пробелы, подтверждённые кодом: нет блокировки трека (locked) в Au4, нет «мастер-дорожки» как сущности, нет пакетного экспорта/заданий, нет LLM/голосового/очистки — всё с нуля (см. таблицу 10 в map-файле).

**Принятые решения и почему:**
- Анализ вести только по реальному коду (правило §2), поэтому каждый вывод в map-файле снабжён ссылкой на файл/строку.
- Не создавать `docs/plans/roadmap.md` в этой сессии — пользователь явно ограничил сессию ШАГОМ 1.

**Осталось / следующая сессия:**
1. Получить ответы на вопросы из раздела 11 map-файла (обязательно: точный паттерн имён WAV-референсов; JSON-структура; модель тейков; API голосового сервиса; LLM-провайдер; движок AI-очистки; хранение референсов внутри .aup3 или рядом; наличие Qt 6.10).
2. ШАГ 2 АРХИТЕКТУРА: план по пунктам раздела 6 AGENTS.md с файлами/классами.
3. ШАГ 3: `docs/plans/roadmap.md` — модули, риски, критерии готовности.

**Открытые вопросы:** см. раздел 11 `docs/analysis/audacity4_map.md`.
