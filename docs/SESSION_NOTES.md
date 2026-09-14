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

---

## Сессия 2026-09-14 (вторая) — ШАГ 2 + ШАГ 3: архитектура и roadmap

**Сделано:**
- Прочитан AGENTS.md v2 целиком; ответы на вопросы Шага 1 из него учтены
  (паттерн `{guid}.wav`, JSON-структура — sample.json подтверждён, тейки =
  отдельные дорожки, провайдеры voice/LLM настраиваемые, очистка = внешний
  Python-процесс, референсы внутри .aup3, Qt 6.10 MSVC 2022).
- Закрыты три замечания к map-файлу (ревизия 2):
  1) §5 переснят дословно: `BuiltinEffectsModule::Registration<T>`
     (LoadEffects.h:29-47) + preInit-регистрации (builtincollectionloader.cpp:80+)
     + ревамп audioplugins (builtineffectsmodule.cpp resolveImports →
     IAudioPluginsScannerRegister / IAudioPluginMetaReaderRegister /
     IEffectLoadersRegister) + минимальные живые примеры fade (без UI) и
     amplify (QML: AmplifyViewModelFactory.createModel + regUrl);
  2) §7: дословные цитаты CMakeLists — корень добавляет только
     muse/framework + src + share (CMakeLists.txt:205-207); au3wrapDefs.cmake:22-23
     (AU3_LIBRARIES=au3/libraries, AU3_MODULES=au3/modules); au3wrap добавляет
     только libraries и modules/import-export (CMakeLists.txt:86,89);
     au3/src target Audacity (au3/src/CMakeLists.txt:5-15) вне сборки;
     au3-menus закомментирован (au3/libraries/CMakeLists.txt:79);
  3) путь фреймворка: muse/framework (CMakeLists.txt:24-25), не muse_framework.
- Создан `docs/plans/architecture.md` (ШАГ 2): по каждому пункту §6 —
  механизм, файлы/классы, отмена; 5 новых muse-модулей (dubbing,
  dubbing_text, dubbing_cleanup, dubbing_voice, dubbing_jobs) + расширения
  существующих; точка регистрации панелей ProjectPage.qml panels;
  оценка размера .aup3 (10k реплик ≈ 14 ГБ, порог внешних ссылок 15 ГБ /
  2–3 мин сохранения).
- Создан `docs/plans/roadmap.md` (ШАГ 3): M0 (сборка+тесты, команда ctest
  фиксируется по факту) + M1–M12 (1 модуль = 1 PR): для каждого файлы/классы,
  механизм отмены, сложность/риски, критерий готовности, переиспользование;
  раздел «Отложено: WEM»; явные вопросы.

**Ключевые новые установленные факты (добавлены в этой сессии):**
- Механизм отмены для метаданных дубляжа: `UndoStateExtension` +
  `UndoRedoExtensionRegistry::Entry<T>` (au3-project-history/UndoManager.h:84-131),
  вызов через `IProjectHistory::modifyState(typeid(...))`
  (src/trackedit/iprojecthistory.h:44-51) — параллельный undo не нужен.
- Запись blob данных дубляжа: `ProjectSerializer::WriteBlob`
  (au3-project-file-io/ProjectSerializer.h:61) в `OnUpdateSaved`
  ProjectFileIOExtension.
- Программный вызов штатных эффектов (для 6.10):
  `IEffectExecutionScenario::performEffect(effectId, params)`
  (src/effects/effects_base/ieffectexecutionscenario.h:23-24).
- AU4 сохраняет новые проекты как .aup4 (projecttypes.h:297-299:
  AUP3/AUP4/AUP4UNSAVED) — контейнер тот же SQLite; вынесен вопрос №1.
- Панели: DockPanel в ProjectPage.qml `panels: [...]`, имя через
  ProjectPageModel::*PanelName() (projectpagemodel.h:44-46), открытие
  действием dock-set-open.
- Импорт: `Au3Importer::importIntoTrack(filePath, dstTrackId, startTime)`
  (au3importer.h:35); экспорт: `IExporter::exportData`
  (iexporter.h:43-44).

**Принятые решения и почему:**
- 5 muse-модулей вместо одного src/dubbing: изоляция AI-сервисов от ядра
  (правила Muse: модуль = домен), плюс PR-и инкрементальность; «1 модуль =
  1 PR» трактуется как пакет работ roadmap'а (несколько PR могут
  расширять один muse-модуль) — явно оговорено в обоих документах.
- Тейки = отдельные дорожки через newMonoTrack + контроллер дубляжа
  (штатный punch/loop отклонён пользователем в §4.3).
- Locked-референс: флаг в track.h + гвард в trackeditinteraction (своего
  флага в Au4 нет — подтверждено).

**Осталось / следующая сессия:**
1. Ответы на вопросы roadmap §4 (aup4 vs aup3; порог 15 ГБ; порядок
   M9–M11; тип лимита voice-сервиса; движки очистки; снятие запрета на код).
2. После явного разрешения — M0 (сборка на машине, фиксация команды
   тестов) и M1 (ветка/PR ядра домена).

**Открытые вопросы:** `docs/plans/roadmap.md` §4 (6 вопросов).

### Доработка той же сессии (по двум уточнениям пользователя)

- Получен реальный масштаб: 39 481 реплика / 2 181 сцена / 42 файла игры.
- Пересчитана оценка размера (architecture.md §14, roadmap.md §2):
  формула `V = N × (T̄ref×Bref + Ktake×Ttake×Btake + T̄master×Bmaster)`;
  допущения: 48 кГц моно, референсы 24-bit (0.144 МБ/с, T̄=2.1 c по
  sample.json), тейки 32-float (0.192 МБ/с, 2 шт × 2.3 c), мастер 24-bit;
  ≈1.49 МБ/реплику → монолит всей игры ≈ 58.7 ГБ — порог 15 ГБ превышен
  почти в 4 раза → монолит отклонён; рекомендована стратегия «один
  дубляж-проект = один файл игры» (42 проекта, средний ≈1.4 ГБ, максимум
  по оценке ≈7.4 ГБ), порог 15 ГБ/файл остаётся жёстким лимитом, внешние
  ссылки — только резерв. Кэш voice и временные файлы очистки — вне .aup3.
- Вопросы roadmap §4 переписаны полностью (6 шт.) с рекомендациями,
  включая полный разбор .aup4 vs .aup3 (рекомендация .aup4: штатный путь
  AU4, контейнер тот же SQLite).
- Код по-прежнему не пишется; ожидается согласование roadmap (вопрос №6).

---

## Сессия 2026-09-14 (третья) — согласование roadmap, старт M0/M1

**Сделано:**
- Пользователь согласовал architecture.md и roadmap.md. Решения (занесены
  в roadmap.md §4 «Решения»): расширение .aup4; один дубляж-проект = один
  файл игры (42 проекта), порог 15 ГБ/файл жёсткий, внешние ссылки —
  резерв; порядок M9→M10→M11→M12; бюджет voice-сервиса — абстрактные
  единицы расхода (по умолчанию символы), API не фиксировать до M11;
  очистка DeepFilterNet (мощный) + RNNoise (слабый), Python venv вручную.
- Запрет на код (AGENTS.md §2 п.6) снят ЧАСТИЧНО: разрешены только
  M0 (сборка + фиксация команды ctest) и M1 (ядро домена, отдельная
  ветка). Остальные модули — только после отдельного согласования.

**Далее:** M0 — проверить окружение, конфигурация/сборка audacity-debug,
прогон ctest, фиксация фактической команды в roadmap.md; затем M1 — ветка,
каркас src/dubbing, персистентность, тип проекта, тест, пакет
доказательств по AGENTS.md §9 (git diff --stat, лог сборки, лог теста,
git log, инструкция ручной проверки).

### Итоги M0 + M1 (та же сессия, 2026-09-14)

**M0 (ВЫПОЛНЕНО):**
- Окружение: VS 18 BuildTools MSVC 14.50 (cl 19.50), CMake 4.2.3-msvc3 +
  Ninja (из состава VS), Qt 6.10.1 msvc2022_64.
- Пресет `audacity-debug` НЕ собирается на этой машине: muse_deps использует
  prebuilt только при RelWithDebInfo (resolve.cmake:460-461), source-fallback
  падает на libpng/ZLIB. Рабочий пресет — `audacity-release`.
- По требованию пользователя выполнена ПОЛНАЯ ЧИСТАЯ пересборка (удалены
  build/, build.release/, build.install/, старый exe 11:41 → свежий 12:26):
  3319 целей, exit 0.
- ctest требует PATH (Qt bin + все _deps\*\bin + _deps\wxwidgets\lib\vc_x64_dll);
  команды зафиксированы в roadmap.md §M0.
- Результат: 28/29 (после M1; до M1 — 27/28). Единственный красный:
  `au_project_tests::Load_FileCannotBeOpened_ReturnsCantOpen` — окруженческий
  (read-protected файл открывается под этим пользователем), воспроизводится
  на чистой сборке, наших изменений не касается.

**M1 (ВЫПОЛНЕНО, ветка feature/dubbing-m1-core, коммит 4022342b6,
17 файлов, +771/−12):**
- Создан модуль `src/dubbing`: dubbingtypes.h (домен), DubbingProject
  (attached-объект + XMLTagHandler), DubbingStateExtension (undo),
  DubbingModule (линковка регистраций), тесты + environment (SuiteEnvironment
  с Au3WrapModule — без него SEH в AudacityProject::Create).
- Ключевое уточнение механизма (зафиксировано в roadmap): персистентность —
  НЕ ProjectFileIOExtension/WriteBlob (OnUpdateSaved вызывается после записи
  doc), а штатный `ProjectFileIORegistry` (XMLMethodRegistry<AudacityProject>):
  ObjectWriterEntry пишет `<dubbing>` в корень `<project>` при каждом
  Save/AutoSave (ProjectFileIO.cpp:1862 CallWriters), ObjectReaderEntry
  читает при Load. Undo — UndoStateExtension::RestoreUndoRedoState через
  ProjectHistory::PopState.
- Проводка: src/CMakeLists.txt, appfactory.cpp, src/app/CMakeLists.txt
  (add_to_link_if_exists), ProjectCreateOptions.dubbing, опция
  AU_BUILD_DUBBING_TESTS.
- Грабли, собранные по ходу: attached-объект обязан наследовать
  ClientData::Base; `AttachedObjects` (не AttachedProjectObjects) — алиас
  внутри AudacityProject; TrackList живёт в au3-track/Track.h; тестам нужен
  wxBase в link и muse SuiteEnvironment.
- Тест `dubbing_tests` 3/3 OK (undo/redo текста; round-trip save/load через
  Au3ProjectAccessor — домен идентичен; обычный проект не дубляж).
  Полный набор: 28/29.
- В M1 НЕ вошло (перенесено в M2): IOC-интерфейс idubbingproject,
  настройки dubbingconfiguration, диалог выбора типа при создании.

**Фикс assert-диалога wxWidgets (по требованию пользователя, коммит ff6fa31a3):**
пользователь увидел «wxWidgets Debug Alert: DBConnection.cpp(703): assert
!mpConnection failed — Project file was not closed at shutdown». Разбор:
ConnectionPtr — attached-объект AudacityProject с unique_ptr<DBConnection>;
закрытие — ProjectFileIO::CloseProject() (вызывается из
Au3ProjectAccessor::close(), au3project.cpp:308). Причину держал НЕ наш код
(DubbingProject/DubbingStateExtension не хранят соединений и IAu3Project;
dubbing_tests закрывает оба accessor'а — потому в наших прогонах зависаний
не было), а тест апстрима Load_FileCannotBeOpened: на Windows
FILE_ATTRIBUTE_HIDDEN не запрещает чтение → load успешен → close не вызван
(«can't close») → ConnectionPtr::~ConnectionPtr с открытым соединением →
assert-диалог; хендлы держат empty_read_protected.aup4 (+wal/shm) — прямая
причина «removeIfExists: failed» в логе (связь подтверждена: после фикса
строки исчезли). Также testtools::removeIfExists теперь сбрасывает
read-only/hidden перед std::remove (мусор write-protected кейсов).
Итог: au_project_tests — 8 passed + 1 skip (Windows, осознанно); полный
ctest — 100%, 29/29; в data/ мусора нет. M1 повторно подтверждён полным
зелёным прогоном.

**Дистрибутив для ручной проверки (по запросу пользователя):**
`cmake --install build\audacity-release --prefix D:/auda/audacity/dist` —
self-contained каталог `dist/` (bin/Audacity4.exe + Qt6*/MSVC/сторонние DLL,
qml, plugins, translations, nyquist, qt.conf; windeployqt отработал).
Smoke-тест как в CI: `dist\bin\Audacity4.exe --plugin-registration-self-test`
→ exit 0. Команда внесена в roadmap.md §M0.

**Открытые вопросы:** нет новых; ждём ревью M1 и разрешения на M2.

---

## Сессия 2026-09-14 (четвёртая) — M2: импорт JSON + массовый импорт WAV

**Сделано (ветка feature/dubbing-m2-import от feature/dubbing-m1-core):**
- `src/dubbing/dubbingconfiguration.h` — паттерн «{guid}.wav» (QRegularExpression,
  группа 1 = guid) и порог расхождения длительности 0.1 c константами
  (вынос в настройки — M3).
- `src/dubbing/idubbingproject.h` — IOC-интерфейс (importFromJson /
  importWavFolder / setLineRu / domainChanged) + структуры результатов
  (счётчики, warnings-расхождения, ошибки).
- `src/dubbing/import/dubbingjsonreader.*` — QJsonDocument для значений +
  структурный сканер порядка ключей (QJsonObject сортирует ключи!);
  пустой speaker_name -> UNKNOWN; dur обязателен; дубликаты guid — ошибка.
- `src/dubbing/import/dubbingimportservice.*` — рекурсивный скан
  (QDirIterator), сопоставление по guid (upper-case), сверка dur через
  IImporter::fileInfo ДО импорта, ленивое создание дорожки «REF <file_id>»
  (ITracksInteraction::addWaveTrack(1) + changeTrackTitle), импорт блоком
  IImporter::importIntoTrack, ClipKey через DomAccessor::findWaveClip,
  курсор по ФАКТИЧЕСКОМУ концу клипа; историю НЕ пушит (пушит DubbingService).
- `src/dubbing/internal/dubbingservice.*` — реализация IDubbingProject:
  инкрементальное слияние JSON (существующие реплики не трогаются),
  ОДИН pushHistoryState(CONSOLIDATE) на пакет («Импорт метаданных
  дубляжа» / «Импорт дубляжа»), setLineRu — отдельный пуш.
- `dubbingmodule.*` — DubbingContext::registerExports (паттерн
  ImporterModule/ImporterContext: IDubbingProject — контекстный экспорт).
- ДОМЕН M1 ИЗМЕНЁН: NO_TRACK_ID/NO_CLIP_ID = -1 (0 — валидный id:
  TrackList::sCounter = -1); dubbingtypes.h, сериализация не изменилась
  (id пишутся всегда).
- Тесты `dubbingimport_tests.cpp` (6 кейсов) + data/sample.json (копия
  docs/requirements) + data/mini_unknown.json (кейс UNKNOWN):
  порядок ключей (первый guid 6046…, алфавитно первым был бы 039D…),
  2 файла / 3 сцены / 47 реплик; NoReference (44 без WAV); расхождение
  dur (2.2 против 1.732); инкрементальность (правка ru сохранена, клипы
  не дублируются, новый WAV добавляется без наложений); undo (InitialState
  + 2 пуша; отмена возвращает мета без файлов + нет REF-дорожек);
  setLineRu undo/redo.
- Результаты: dubbing_tests 9/9 (3 M1 + 6 M2); ПОЛНЫЙ ctest — 29/29 (100%),
  audacity.exe собран. Пуш ветки не делался (по заданию — после ревью).

**Грабли, собранные по ходу (важно для следующих модулей):**
1. Importer::Initialize() снимает снапшот реестра импорт-плагинов через
   std::call_once — RegisterImportPlugins() в тестовом окружении нужно
   звать ДО onAllInited (setPreInit), иначе список пуст навсегда.
2. PCM-импорт репортит прогресс -> BasicUI::MakeProgress -> Au3BasicUI с
   activeContext()==null в консоли -> ProgressDialog с нулевым контекстом
   -> ContextInject по нулевому ctx роняет IOC-разрешение (SEH). Лечение:
   headless-BasicUI (без IOC) в setPostInit поверх Au3BasicUI.
3. kors ioc(globalCtx()) при IOC_CHECK возвращает nullptr (globalId == 0,
   assert id>0) — контекстные зависимости в тестах регистрируются в
   собственном контексте с id>0; Inject-поля внутренних классов trackedit/
   importer приватны (friend только штатным тестам) — .set() недоступен.
4. SelectionControllerMock без состояния ломает importIntoTrackInternal:
   setSelectedTracks(no-op) -> selectedTracks() пуст -> paste уходит в
   pasteIntoNewTracks. Мок должен хранить выделение.
5. muse::io::path_t::toString() возвращает QString (нужен .toStdString()).
6. MSVC: локальные классы в функциях + unique_ptr-конверсии — выносить
   в область имён; в dubbingjsonreader.cpp var «file» затеняет QFile file
   (warning C4456 — безвредно, но лучше переименовать при случае).
7. ITracksInteraction::addWaveTrack возвращает TrackId БЕЗ пуша истории —
   правильный путь для пакетного создания дорожек; newMonoTrack (из
   ITrackeditInteraction) пушит историю на каждую дорожку.
8. UndoRedoExtensionRegistry-сейвер снимает мету в ОДИН пуш вместе с
   дорожками — undo пакета возвращает и аудио, и метаданные атомарно
   (подтверждено тестом d).

**Открытые вопросы:** нет. Перенесено в M3: диалог импорта QML (прогресс/
лог), настройки паттерна/порога, Q_INVOKABLE-QML-обёртки, фоновый поток.

---

## Сессия 2026-09-14 (пятая) — M2-followup: единый undo-шаг + тест соседних расхождений

**Сделано (ветка feature/dubbing-m2-import, по трём требованиям владельца
к условно принятому M2):**
- П.3 (единый undo-шаг): доказано по коду — ограничения со стороны au3 НЕТ,
  PushState зовёт вызывающий (UndoManager.cpp:237-265; CONSOLIDATE сливает
  только одинаковые описания подряд, 241-244). Добавлен объединённый
  `IDubbingProject::importProject(jsonPath, wavFolder)`: этапы JSON -> WAV
  через приватные `doImportJson` / `doImportWav` (общий код, БЕЗ пушей —
  не дублируется), в завершение ОДИН
  `pushHistoryState(«Импорт дубляжа», CONSOLIDATE)`. Самостоятельные
  `importFromJson` / `importWavFolder` сохраняют свои пуши — гранулярность
  осознанная (точечный API M3+). Результат — `ProjectImportResult
  { json, wav, ok }`.
- П.3 (тесты): `UndoRestoresPreImportState` переведён на importProject:
  явная проверка `UndoManager::Get(project).GetNumStates() == 2`
  (InitialState + ОДНО импортное состояние); ОДИН undo возвращает мета
  без файлов + нет REF-дорожек.
- П.2 (новый тест): `NeighbourMismatchDuration_CorrectClipMapping` —
  соседние расхождения в РАЗНЫЕ стороны (A: dur 1.861 -> WAV 1.2 с;
  B: 1.075 -> 1.9; C: 1.732 -> 1.0; гуйды первой сцены sample.json),
  через importProject: клипы подряд без наложений (start[i+1] == end[i],
  допуск 1e-6), длительность каждого клипа == длительности ЕГО WAV
  (не dur), ClipKey A/B/C -> первый/второй/третий клип дорожки
  (Intervals()[i]->GetId()), предупреждений — 3 (знаки diff: -/+/-).
- П.1: хвост лога сборки снят (см. отчёт сессии).
- docs: roadmap §M2 «Механизм отмены» и примечание 6 переписаны под
  importProject (один push; независимые методы — свои пуши).

**Результаты:** сборка vcvars64 + `cmake --build build\audacity-release
--target dubbing_tests audacity` — exit 0 (Audacity4.exe слинкован);
`dubbing_tests.exe` — **10/10** (3 M1 + 7 M2, включая новый);
полный ctest — **29/29 (100%)**, dubbing_tests в ctest — 3.47 c.

**Открытые вопросы:** нет. M2 закрыт; пуш ветки и ff-обновление master
выполнены по разрешению владельца.

---

## Сессия 2026-09-14 (шестая) — M3: панель списка реплик

**Сделано (ветка feature/dubbing-m3-panel от master @ 9fd6c4dfd):**
- C++: `panel/lineslistmodel.*` (плоская модель, data()/rowCount() O(1),
  роли: guid/файл/сцена/статус+русский текст/спикер/EN/RU/длительности/
  расхождение/ссылки; searchBlob — предвычисленный lowercase для поиска),
  `panel/linesfiltermodel.*` (QSortFilterProxyModel: UNKNOWN/статус/
  расхождение/без референса + поиск; begin/endFilterChange — Qt 6.10
  deprecated invalidateFilter), `panel/lineworkspacecontroller.*`
  (openLine: выделение клипа + setLastPlaybackSeekTime; lineInfo; setRuText
  через IDubbingProject::setLineRu).
- QML: `qml/Audacity/Dubbing/LinesPanel.qml` + qmldir + `dubbing.qrc`;
  регистрация типов в DubbingModule::registerUiTypes (URI
  «Audacity.Dubbing»), registerResources (Q_INIT_RESOURCE).
- Регистрация панели (architecture §0.2 дословно): DockPanel в
  ProjectPage.qml `panels: [...]`, `linesPanelName()` в ProjectPageModel,
  LINES_PANEL_NAME в appshelltypes.h, действие toggle-lines
  (ApplicationUiActions + карта toggleDockActions) + пункт меню
  «Вид -> Реплики» (appmenumodel.cpp). По умолчанию скрыта.
- Домен: `Line::actualDur` (фактическая длительность WAV, -1 = неизвестно;
  заполняется на импорте и для уже импортированных), XML-атрибут
  `actual_dur` опционален (старые файлы читаются, версия схемы не менялась,
  round-trip доказан тестом). IDubbingProject += domainSnapshot() /
  isDubbingProject().
- Модель следует undo/redo: подписка на IProjectHistory::historyChanged
  (DubbingStateExtension восстанавливает домен молча — как HistoryPanelModel).
- Тесты `dubbingpanel_tests.cpp` (7 кейсов): роли/порядок; все фильтры;
  поиск (RU/EN/guid/сцена, регистронезависимо); 30 000 реплик с замерами
  (build 45 мс, фильтр 12 мс/6000, поиск 6 мс, 10 страниц по 50 строк
  0.28 мс, 100k rowCount 2.7 мс); openLine (EXPECT_CALL на выделение +
  позицию, референс-отрицание, lineInfo); правка из панели с undo/redo и
  автообновлением модели; actualDur round-trip через save/load.
- Результаты: dubbing_tests **17/17**; полный ctest **29/29 (100%)**;
  audacity.exe собран; смок `--plugin-registration-self-test` exit 0.

**Грабли, собранные по ходу (важно для следующих модулей):**
1. Inject-поля muse НЕ имеют члена `.val` — только `get()/operator()/set()`;
   проверки на null: `if (auto x = inject())`. Inject-поля для .set() из
   тестов обязаны быть в public-секции класса.
2. Тестовый фиксчер с реальным importProject ОБЯЗАН мокать
   `TrackeditProjectMock::trackList()` реальными дорожками (DomConverter)
   — иначе addWaveTrack/paste падают SEH 0xc0000005 (в M2-фиксчере был,
   при копировании в M3 пропущен — найден бисекцией с cerr-метками;
   cout через пайп теряется при SEH, cerr — нет).
3. QSortFilterProxyModel Qt 6.10: invalidateFilter() deprecated —
   beginFilterChange()/endFilterChange() вокруг смены критериев.
4. Фильтр «расхождение» должен сверять actualDur с dur ИЗ JSON, а не с
   DurRole (та возвращает фактическую, если известна — diff всегда 0);
   для этого в модели DeclaredDurRole.
5. QObject-прокси нельзя переприсваивать (`filter = LinesFilterModel{}`) —
   сбрасывать сеттерами.
6. gmock-матчеры типа DoubleNear не конвертятся в Matcher<number_t<double>>
   — снимать значения Invoke-ом.
7. QML: у C++ QAbstractListModel rowCount() вызывается из QML (прецеденты
   в project); count надёжнее брать у ListView; StyledDropdown принимает
   model как массив {text, value}; в теме НЕТ danger/warning-цветов —
   использованы stroke/link/accent/button.
8. QRC-модуль QML: qmldir с `module Audacity.Dubbing` в qrc (prefix «/»),
   движок имеет «:/qml» в путях импорта (uiengine.cpp:77) — отдельная
   регистрация пути не нужна.
9. Смук приложения — ТОЛЬКО из dist (после cmake --install): запуск
   src/app/bin/Audacity4.exe из дерева сборки даёт предупреждение
   «Critical Nyquist files could not be found. Nyquist effects will not
   work.» (NyquistEffectsModule::Initialize ищет nyquist-runtime\nyquist.lsp
   относительно exe, каталог деплоится только install). Окруженческое,
   не регрессия: смок из dist — exit 0 без диалога. Зафиксировано
   в roadmap §M0.

**Расхождения с планом (формат AGENTS.md §9):** категория 1 (технические
детали): имена файлов (опечатка плана linesslistmodel -> lineslistmodel);
«скролл таймлайна» двойного клика реализован как выделение клипа +
позиция воспроизведения (публичного API горизонтального скролла в AU4
нет, TimelineContext приватен для projectscene — вид уходит к реплике
при старте воспроизведения); панель по умолчанию скрыта (открывается
«Вид -> Реплики»). Изменений контракта (категория 2) нет: правка RU —
по-прежнему ОДИН pushHistoryState на правку; формат .aup4 расширен
опциональным атрибутом actual_dur с сохранением обратной совместимости.

**Открытые вопросы:** нет. Пуш ветки — после ревью владельца (как с M2).

### Ручная проверка M3 через exe (дополнение сессии)

Замечание владельца: диалог импорта QML был ПЕРЕНЕСЁН из M2 (roadmap §M2
п.8), но в сессию M3 не вошёл (формулировка задачи — только панель §6.3) —
из UI домен пока нечем наполнить. Поэтому для ручной проверки добавлен
тест-генератор готового проекта:

1. Пересобрать/прогнать: `dubbing_tests.exe
   --gtest_filter=DubbingPanelTests.ManualCheck_CreateDemoProject` —
   создаёт `D:\auda\audacity\manual_check\m3_dubbing_demo.aup4`
   (sample.json: 2 файла / 3 сцены / 47 реплик, 3 референса на дорожке
   REF, 1 расхождение длительности, 44 «нет референса», 1 правка RU).
   Каталог manual_check/ — в .gitignore.
2. Обновить dist: `cmake --install build\audacity-release --prefix
   D:/auda/audacity/dist` (смук — ТОЛЬКО из dist, см. грабли №9).
3. `dist\bin\Audacity4.exe` -> «Файл -> Открыть» -> m3_dubbing_demo.aup4.
4. «Вид -> Реплики»: проверить список/колонки, фильтры (UNKNOWN/статус/
   расхождение/без референса), поиск, двойной клик (выделение клипа +
   плейхед; Space — воспроизведение от реплики), правку RU в рабочей
   зоне и Ctrl+Z / Ctrl+Shift+Z, перетаскивание панели.
   Примечание: у 44 реплик «нет референса» двойной клик не позиционирует
   (нет клипа) — панель показывает статус «Нет референса».
5. Итог прогона после добавления генератора: dubbing_tests 18/18.

**Открытые вопросы (новые):** (1) UI-импорт (перенос из M2: диалог
JSON+папка, прогресс/лог) и диалог выбора типа проекта при создании —
предлагаю оформить отдельным модулем M3-followup (та же ветка или новая);
жду решения владельца.

### Дополнение той же сессии — редизайн панели по требованию владельца

Требование: раскрывающиеся заголовки из JSON; файл (первый уровень) —
Select НАД списком; в строке достаточно: статус, спикер, EN и RU друг
над другом, длительность в конце.

**Сделано:**
- `LinesListModel`: `fileId`/`fileIds()` (авто-выбор первого файла) +
  `toggleScene(sectionKey)` / `setAllScenesExpanded(bool)`; строки-роли
  `RowTypeRole` (реплика/заголовок), `SectionKey/Title/LineCount/Expanded`;
  первая сцена файла по умолчанию раскрыта; свёрнутость хранится по ключу
  файл+сцена и переживает переключение Select'а. Режим
  `filteringActive=true`: плоский список ВСЕХ реплик выбранного файла
  (включая свёрнутые секции!) без заголовков — фильтры/поиск обязаны
  видеть свёрнутое; QML синхронизирует режим (syncFilteringMode).
- `LinesFilterModel`: при активных фильтрах заголовки скрываются
  (дубль-защита — в поисковом режиме их и так нет).
- `LinesPanel.qml`: Select файла, поиск, фильтры + «Свернуть/Развернуть
  всё», делегат-загрузчик (заголовок: стрелка + quest_id + счётчик;
  реплика: маркер статуса · спикер · EN(0.65)/RU(жирн. при выборе) ·
  длительность справа, подсветка при расхождении), рабочая зона без
  изменений.
- Тесты: + `Hierarchy_FileSelectAndSections` (50 файлов), 30k-домен
  переделан в ОДИН файл 600x50 (Select ограничивает список файлом —
  нагрузка обязана быть в одном файле), ModelBuild переписан под
  иерархию, PanelTextEdit ищет строку сканом по guid.
- Результаты: dubbing_tests **19/19**; полный ctest **29/29 (100%)**;
  dist пересобран и переустановлен, смок exit 0; демо-проект
  regenerated. Замеры (1 файл, 30k/600 сцен): collapsed build 10.6 мс,
  expandAll 30600 строк 55.7 мс, фильтр 16.9 мс, поиск 6.0 мс,
  вьюпорт-страница ~0.13 мс, rowCount x100k 3.2 мс.

**Грабли редизайна:** (1) std::vector<QString> — нет isEmpty()/first()
(это не QStringList); (2) лямбда, захватывающая unique_ptr по значению,
не компилируется — захват по ссылке; (3) фильтр по СВЁРНУТЫМ секциям —
режим filteringActive в модели (прокси не видит свёрнутое: строк нет
в раскладке); (4) «свернуть всё» обязан явно фиксировать false для всех
секций, иначе первая снова развернётся (default-first-expanded);
(5) ВАЖНО (баг ручной проверки): Component, объявленный ВНЕ делегата
ListView и инстанцируемый через Loader, не видит контекстных свойств
делегата — model.* = undefined -> пустые тексты и мёртвые клики
(Select при этом работал — он читает fileIds() напрямую). Лечение:
единый делегат ListItemBlank с содержимым, переключаемым по rowType
(visible), без Loader/Component; (6) запущенный dist\bin\Audacity4.exe
блокирует cmake --install (Permission denied) — закрывать перед
переустановкой; (7) muse CheckBox — НЕ QtQuick.Controls: сигнал
clicked без параметров, checked сам НЕ переключается — в обработчике
`checked = !checked` вручную (иначе чекбоксы «не работают»);
(8) `model.rowCount` в QML — method-object («function () { [native
code] }» в тексте) — для счётчика добавлено Q_PROPERTY count READ
rowCount NOTIFY reloaded; (9) RowLayout не переносит детей — для
тулбара в узкой панели использовать Flow.
