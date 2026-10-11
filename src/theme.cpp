// Theme watching follows Omacut (MIT, David Heinemeier Hansson).
#include "theme.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <cmath>

namespace {

// Traditional watercolour pigments, used when there's no Omarchy theme.
const QList<QPair<QString, QString>> kPigments = {
    {"red", "#b8442e"},  {"orange", "#c18a2a"}, {"yellow", "#cfa53a"},  {"green", "#6b8a45"},
    {"cyan", "#4f8f80"}, {"blue", "#34507e"},   {"magenta", "#a8506e"}, {"brown", "#6f4e33"},
};

} // namespace

Theme::Theme(const QString &directory, QObject *parent)
    : QObject(parent),
      m_directory(directory.isEmpty() ? QDir::homePath() + "/.local/state/omarchy/current" : directory)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);
    reload();
}

QString Theme::readColor(const QString &path, const QString &key, const QString &fallback)
{
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QRegularExpression expression(QStringLiteral(R"re(^\s*%1\s*=\s*["'](#[0-9a-fA-F]{6})["'])re")
                                                .arg(QRegularExpression::escape(key)),
                                            QRegularExpression::MultilineOption);
        const auto match = expression.match(QString::fromUtf8(file.readAll()));
        if (match.hasMatch())
            return match.captured(1);
    }
    return fallback;
}

QString Theme::readAccent(const QString &path) { return readColor(path, "accent", "#FFD60A"); }

// Black or white, whichever reads on the accent (WCAG relative luminance).
QString Theme::accentForeground() const
{
    const QColor c(m_accent);
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    const double luminance = .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
    return luminance > .179 ? "black" : "white";
}

void Theme::reload()
{
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);

    // A theme switch replaces symlinks and files, so watch the parents too and
    // re-arm after every change. Watching the nearest existing ancestor means
    // installing Omarchy later works without restarting the app.
    QString ancestor = m_directory;
    while (!QFileInfo::exists(ancestor) && ancestor != "/")
        ancestor = QFileInfo(ancestor).absolutePath();
    QStringList candidates{ancestor, QFileInfo(ancestor).absolutePath(), m_directory, m_directory + "/theme",
                           m_directory + "/theme/colors.toml"};
    candidates.removeDuplicates();
    for (const auto &path : candidates)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);

    const QString colors = m_directory + "/theme/colors.toml";
    const auto accent = readAccent(colors);
    QStringList pigments;
    for (const auto &[key, fallback] : kPigments)
        pigments << readColor(colors, key, fallback);
    const QString vermilion = pigments.first();

    if (accent != m_accent || pigments != m_pigments || vermilion != m_vermilion) {
        m_accent = accent;
        m_pigments = pigments;
        m_vermilion = vermilion;
        emit changed();
    }
}
