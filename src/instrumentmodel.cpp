#include "instrumentmodel.h"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

#include "instruments.h"

namespace {

QStringList words(const std::string &text)
{
    static const QRegularExpression separator(QStringLiteral("[\\s&-]+"));
    return QString::fromStdString(text).toLower().split(separator, Qt::SkipEmptyParts);
}

bool anyStartsWith(const QStringList &list, const QString &token)
{
    return std::any_of(list.begin(), list.end(), [&](const QString &word) { return word.startsWith(token); });
}

// "frhn" finds French Horn: every letter, in order, somewhere in the name.
bool isSubsequence(const QString &token, const QString &text)
{
    int at = 0;
    for (const QChar c : text)
        if (at < token.size() && c == token[at])
            ++at;
    return at == token.size();
}

int tokenScore(const Preset &preset, const QString &token)
{
    const QString name = QString::fromStdString(preset.name).toLower();
    if (name.startsWith(token))
        return 100;
    if (anyStartsWith(words(preset.name), token))
        return 80;
    if (anyStartsWith(words(preset.family), token))
        return 50;
    if (anyStartsWith(words(preset.tags), token))
        return 40;
    if (name.contains(token))
        return 30;
    if (isSubsequence(token, QString(name).remove(QLatin1Char(' '))))
        return 10;
    return 0;
}

} // namespace

InstrumentModel::InstrumentModel(QObject *parent) : QAbstractListModel(parent) { refilter(); }

int InstrumentModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : count(); }

QVariant InstrumentModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Preset &preset = instruments::catalog()[size_t(m_rows[index.row()])];
    switch (role) {
    case NameRole:
        return QString::fromStdString(preset.name);
    case FamilyRole:
        return QString::fromStdString(preset.family);
    case TagsRole:
        return QString::fromStdString(preset.tags);
    case PresetRole:
        return m_rows[index.row()];
    default:
        return {};
    }
}

QHash<int, QByteArray> InstrumentModel::roleNames() const
{
    return {{NameRole, "name"}, {FamilyRole, "family"}, {TagsRole, "tags"}, {PresetRole, "preset"}};
}

void InstrumentModel::setQuery(const QString &query)
{
    if (query == m_query)
        return;
    m_query = query;
    refilter();
    emit queryChanged();
}

int InstrumentModel::presetAt(int row) const { return row >= 0 && row < m_rows.size() ? m_rows[row] : -1; }

int InstrumentModel::rowOfPreset(int preset) const { return int(m_rows.indexOf(preset)); }

int InstrumentModel::score(const Preset &preset, const QString &query)
{
    const QStringList tokens = query.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (tokens.isEmpty())
        return 1;
    int total = 0;
    for (const QString &token : tokens) {
        const int s = tokenScore(preset, token);
        if (s == 0)
            return 0; // every word typed has to find something
        total += s;
    }
    return total;
}

void InstrumentModel::refilter()
{
    const auto &catalog = instruments::catalog();
    QVector<std::pair<int, int>> ranked; // (score, preset)
    for (int i = 0; i < int(catalog.size()); ++i)
        if (const int s = score(catalog[size_t(i)], m_query))
            ranked.append({s, i});
    // Best match first; the catalog's own order (grouped by family) breaks ties.
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });

    beginResetModel();
    m_rows.clear();
    for (const auto &entry : ranked)
        m_rows.append(entry.second);
    endResetModel();
    emit countChanged();
}
