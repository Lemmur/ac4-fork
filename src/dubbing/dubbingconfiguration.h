/*
* Audacity: A Digital Audio Editor
*
* Константы импорта дубляжа (§6.2 AGENTS.md).
* Вынос в пользовательские настройки — M3 (roadmap §M2): здесь только
* значения по умолчанию, собранные в одном месте.
*/
#pragma once

#include <QString>

namespace au::dubbing {
//! Паттерн имени WAV-референса "{guid}.wav": guid — 32 hex-символа,
//! регистр нечувствителен (AGENTS.md §4.1). QRegularExpression,
//! CaseInsensitiveOption задаётся при использовании; группа 1 — guid.
inline const QString DUBBING_DEFAULT_WAV_NAME_PATTERN = QStringLiteral("^([0-9A-Fa-f]{32})\\.wav$");

//! Порог расхождения фактической длительности WAV с dur из JSON, секунды.
//! Расхождение свыше порога — предупреждение в логе импорта (§6.2).
constexpr double DUBBING_DURATION_MISMATCH_THRESHOLD_SECS = 0.1;
}
