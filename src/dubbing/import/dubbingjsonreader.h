/*
* Audacity: A Digital Audio Editor
*
* Чтение JSON метаданных дубляжа (§6.2, эталон docs/requirements/sample.json):
* {file_id: {quest_id: {guid: {en, ru, speaker_name, speaker_internal, dur}}}}.
*
* Особенность: QJsonObject хранит ключи ОТСОРТИРОВАННЫМИ, а порядок ключей
* JSON — это порядок реплик (AGENTS.md §4.2). Поэтому значения читаются
* через QJsonDocument, а порядок ключей снимается отдельным структурным
* сканом исходного текста (скобочный баланс с пропуском строк).
*/
#pragma once

#include <string>
#include <vector>

#include "io/path.h"

#include "../dubbingtypes.h"

namespace au::dubbing {
class DubbingJsonReader
{
public:
    //! Читает файл UTF-8 и строит дерево GameFile/Scene/Line.
    //! Ошибки (структура, дубликаты guid, нечисловой dur) собираются
    //! в errors; некорректные элементы пропускаются, разбор продолжается.
    bool read(const muse::io::path_t& path, std::vector<GameFile>& outFiles, std::vector<std::string>& errors) const;
};
}
