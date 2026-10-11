#include "profiles.h"

#include <QFile>

#include <set>

#include "instruments.h"
#include "profile.h"

namespace profiles {

QStringList load()
{
    QStringList missing;
    std::set<std::string> wanted;
    for (const Preset &preset : instruments::catalog())
        if (preset.engine == Engine::Spectral)
            wanted.insert(preset.profile);
    for (const std::string &id : wanted) {
        QFile file(QStringLiteral(":/profiles/%1.prof").arg(QString::fromStdString(id)));
        if (!file.open(QIODevice::ReadOnly)) {
            missing << QString::fromStdString(id);
            continue;
        }
        const QByteArray bytes = file.readAll();
        auto profile = Profile::parse(reinterpret_cast<const uint8_t *>(bytes.constData()), size_t(bytes.size()));
        if (!profile) {
            missing << QString::fromStdString(id);
            continue;
        }
        instruments::attachProfile(id, std::move(profile));
    }
    return missing;
}

} // namespace profiles
