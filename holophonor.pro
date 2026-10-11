QT += core gui qml quick quickcontrols2

CONFIG += c++17 release link_pkgconfig
PKGCONFIG += libpipewire-0.3 alsa
TARGET = holophonor
TEMPLATE = app

HEADERS += \
    src/audioengine.h \
    src/backend.h \
    src/control.h \
    src/dsp.h \
    src/inkcanvas.h \
    src/inkpainter.h \
    src/instrumentmodel.h \
    src/instruments.h \
    src/midiinput.h \
    src/paper.h \
    src/reverb.h \
    src/synth.h \
    src/theme.h

SOURCES += \
    src/main.cpp \
    src/audioengine.cpp \
    src/backend.cpp \
    src/inkcanvas.cpp \
    src/inkpainter.cpp \
    src/instrumentmodel.cpp \
    src/instruments.cpp \
    src/midiinput.cpp \
    src/paper.cpp \
    src/reverb.cpp \
    src/synth.cpp \
    src/theme.cpp

RESOURCES += src/resources.qrc
