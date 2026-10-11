// The instrument list behind the top-left menu, filtered as the player types.
#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

struct Preset;

class InstrumentModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role { NameRole = Qt::UserRole + 1, FamilyRole, TagsRole, PresetRole };

    explicit InstrumentModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString query() const { return m_query; }
    void setQuery(const QString &query);
    int count() const { return int(m_rows.size()); }

    Q_INVOKABLE int presetAt(int row) const;
    Q_INVOKABLE int rowOfPreset(int preset) const;

    // How well a preset answers a query; 0 means it doesn't.
    static int score(const Preset &preset, const QString &query);

signals:
    void queryChanged();
    void countChanged();

private:
    void refilter();

    QString m_query;
    QVector<int> m_rows; // preset indices in display order
};
