/*
* Audacity: A Digital Audio Editor
*
* Реализация прокси-модели фильтров панели реплик (M3).
*/
#include "linesfiltermodel.h"

#include <cmath>

#include "lineslistmodel.h"

#include "../dubbingconfiguration.h"

using namespace au::dubbing;

LinesFilterModel::LinesFilterModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    //! Фильтрация на стороне прокси (roadmap §M3), сортировка не используется —
    //! порядок реплик = порядок ключей JSON (§4.2), меняться не должен.
    setDynamicSortFilter(false);
}

bool LinesFilterModel::onlyUnknown() const { return m_onlyUnknown; }
int LinesFilterModel::statusFilter() const { return m_statusFilter; }
bool LinesFilterModel::onlyMismatch() const { return m_onlyMismatch; }
bool LinesFilterModel::onlyNoReference() const { return m_onlyNoReference; }
QString LinesFilterModel::searchText() const { return m_searchText; }

void LinesFilterModel::setOnlyUnknown(bool only)
{
    if (m_onlyUnknown == only) {
        return;
    }
    beginFilterChange();
    m_onlyUnknown = only;
    endFilterChange();
    emit onlyUnknownChanged();
}

void LinesFilterModel::setStatusFilter(int status)
{
    if (m_statusFilter == status) {
        return;
    }
    beginFilterChange();
    m_statusFilter = status;
    endFilterChange();
    emit statusFilterChanged();
}

void LinesFilterModel::setOnlyMismatch(bool only)
{
    if (m_onlyMismatch == only) {
        return;
    }
    beginFilterChange();
    m_onlyMismatch = only;
    endFilterChange();
    emit onlyMismatchChanged();
}

void LinesFilterModel::setOnlyNoReference(bool only)
{
    if (m_onlyNoReference == only) {
        return;
    }
    beginFilterChange();
    m_onlyNoReference = only;
    endFilterChange();
    emit onlyNoReferenceChanged();
}

void LinesFilterModel::setSearchText(const QString& text)
{
    if (m_searchText == text) {
        return;
    }
    beginFilterChange();
    m_searchText = text;
    endFilterChange();
    emit searchTextChanged();
}

bool LinesFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!idx.isValid()) {
        return false;
    }

    const auto roleValue = [&](LinesListModel::Roles role) {
        return sourceModel()->data(idx, role);
    };

    //! 1. Полнотекстовый поиск: guid/файл/сцена/спикер/EN/RU
    //!    (предвычисленный blob модели уже в нижнем регистре).
    if (!m_searchText.isEmpty()) {
        if (!roleValue(LinesListModel::SearchBlobRole).toString().contains(m_searchText.toLower())) {
            return false;
        }
    }

    //! 2. Фильтр UNKNOWN: пустой speaker_name -> "UNKNOWN" на импорте (§6.2).
    if (m_onlyUnknown) {
        const QString speaker = roleValue(LinesListModel::SpeakerRole).toString();
        if (!speaker.isEmpty() && speaker.compare(QStringLiteral("UNKNOWN"), Qt::CaseInsensitive) != 0) {
            return false;
        }
    }

    //! 3. Фильтр по статусу.
    if (m_statusFilter >= 0) {
        if (roleValue(LinesListModel::StatusRole).toInt() != m_statusFilter) {
            return false;
        }
    }

    //! 4. Фильтр «расхождение длительности» (порог — dubbingconfiguration.h):
    //! фактическая длительность WAV против dur из JSON (DeclaredDurRole).
    if (m_onlyMismatch) {
        const double actual = roleValue(LinesListModel::ActualDurRole).toDouble();
        const double declared = roleValue(LinesListModel::DeclaredDurRole).toDouble();
        if (actual < 0.0 || std::abs(actual - declared) <= DUBBING_DURATION_MISMATCH_THRESHOLD_SECS) {
            return false;
        }
    }

    //! 5. Фильтр «нет референса».
    if (m_onlyNoReference) {
        if (roleValue(LinesListModel::HasReferenceRole).toBool()) {
            return false;
        }
    }

    return true;
}
