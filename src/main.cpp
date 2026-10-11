// holophonor — breathe into an EWI and paint with sound.

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include "backend.h"
#include "inkcanvas.h"
#include "inkpainter.h"
#include "instrumentmodel.h"
#include "instruments.h"
#include "theme.h"

namespace {

int visualIndex(const QString &name)
{
    const QStringList names{"sumi", "bloom", "graphite", "projection"};
    return int(names.indexOf(name.toLower()));
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("holophonor");
    app.setOrganizationName("holophonor");
    app.setApplicationVersion("0.1.0");

    // Matches the window to holophonor.desktop, so the compositor (Wayland
    // app_id) and the launcher pick up the installed icon.
    app.setDesktopFileName("holophonor");
    app.setWindowIcon(QIcon::fromTheme("holophonor"));

    QCommandLineParser parser;
    parser.setApplicationDescription("Breathe into an EWI and paint with sound.");
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption instrument("instrument", "Start with this instrument (e.g. oboe, alto-sax).", "id");
    const QCommandLineOption visual("visual", "Start with sumi, bloom, graphite or projection.", "name");
    const QCommandLineOption demo("demo", "Play a short demo phrase after starting.");
    const QCommandLineOption silent("silent", "Run the synth without sending sound to PipeWire.");
    const QCommandLineOption screenshot("screenshot", "Save the window to a PNG after --after ms, then quit.", "file");
    const QCommandLineOption after("after", "Delay before --screenshot, in milliseconds (default 2500).", "ms", "2500");
    const QCommandLineOption size("size", "Window size, e.g. 1440x900.", "WxH");
    parser.addOptions({instrument, visual, demo, silent, screenshot, after, size});
    parser.process(app);

    QQuickStyle::setStyle("Basic");
    qmlRegisterType<InkCanvas>("Holophonor", 1, 0, "InkCanvas");
    qmlRegisterUncreatableType<InstrumentModel>("Holophonor", 1, 0, "InstrumentModel", "Provided by the backend");

    Theme theme;
    Backend backend;
    if (parser.isSet(instrument)) {
        const int index = instruments::indexOf(parser.value(instrument).toStdString());
        if (index < 0) {
            qCritical("Unknown instrument '%s'.", qPrintable(parser.value(instrument)));
            return 2;
        }
        backend.setInstrument(index);
    }
    if (parser.isSet(visual)) {
        const int index = visualIndex(parser.value(visual));
        if (index < 0) {
            qCritical("Unknown visual '%s'.", qPrintable(parser.value(visual)));
            return 2;
        }
        backend.setVisual(index);
    }
    backend.start(parser.isSet(silent));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());

    if (parser.isSet(size)) {
        const QStringList parts = parser.value(size).split('x');
        if (parts.size() == 2 && window)
            window->resize(parts[0].toInt(), parts[1].toInt());
    }
    if (parser.isSet(demo))
        QTimer::singleShot(600, &backend, &Backend::playDemo);
    if (parser.isSet(screenshot) && window) {
        const QString path = parser.value(screenshot);
        QTimer::singleShot(parser.value(after).toInt(), window, [window, path] {
            const bool saved = window->grabWindow().save(path);
            if (!saved)
                qCritical("Could not save the screenshot to %s.", qPrintable(path));
            QCoreApplication::exit(saved ? 0 : 1);
        });
    }

    return app.exec();
}
