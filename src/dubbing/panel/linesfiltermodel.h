/*
* Audacity: A Digital Audio Editor
*
* Прокси-модель фильтров панели реплик (M3, §6.3): UNKNOWN-спикеры,
* статус, расхождение длительности, отсутствующий референс +
* полнотекстовый поиск (по предвычисленному нижнему регистру строк —
* один contains на реплику). Один проход фильтрации по 30 000 строк —
* миллисекунды;.delegate-виртуализация остаётся на стороне ListView.
*/
#pragma once

#include <QSortFilterProxyModel>

#include "../dubbingtypes.h"

namespace au::dubbing {
class LinesFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT

    Q_PROPERTY(bool onlyUnknown READ onlyUnknown WRITE setOnlyUnknown NOTIFY onlyUnknownChanged)
    Q_PROPERTY(int statusFilter READ statusFilter WRITE setStatusFilter NOTIFY statusFilterChanged)
    Q_PROPERTY(bool onlyMismatch READ onlyMismatch WRITE setOnlyMismatch NOTIFY onlyMismatchChanged)
    Q_PROPERTY(bool onlyNoReference READ onlyNoReference WRITE setOnlyNoReference NOTIFY onlyNoReferenceChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)

public:
    explicit LinesFilterModel(QObject* parent = nullptr);

    bool onlyUnknown() const;
    int statusFilter() const;          //!< LineStatus или -1 = все статусы
    bool onlyMismatch() const;
    bool onlyNoReference() const;
    QString searchText() const;

public slots:
    void setOnlyUnknown(bool only);
    void setStatusFilter(int status);
    void setOnlyMismatch(bool only);
    void setOnlyNoReference(bool only);
    void setSearchText(const QString& text);

signals:
    void onlyUnknownChanged();
    void statusFilterChanged();
    void onlyMismatchChanged();
    void onlyNoReferenceChanged();
    void searchTextChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    bool m_onlyUnknown = false;
    int m_statusFilter = -1;
    bool m_onlyMismatch = false;
    bool m_onlyNoReference = false;
    QString m_searchText;
};
}
