#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QStringList>
#include <QTimer>

// Follows the Omarchy theme live. The accent lights the Projection and the
// menus; the theme's eight colours become the watercolour pigments, so a
// theme switch repaints the instrument's palette too.
class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(QString vermilion READ vermilion NOTIFY changed)
    Q_PROPERTY(QStringList pigments READ pigments NOTIFY changed)

public:
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr);

    QString accent() const { return m_accent; }
    QString accentForeground() const;
    QString vermilion() const { return m_vermilion; }
    QStringList pigments() const { return m_pigments; }

    static QString readAccent(const QString &path);
    static QString readColor(const QString &path, const QString &key, const QString &fallback);

signals:
    void changed();

private:
    void reload();

    QString m_directory;
    QString m_accent = "#FFD60A";
    QString m_vermilion;
    QStringList m_pigments;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};
