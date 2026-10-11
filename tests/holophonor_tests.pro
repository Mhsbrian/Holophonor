QT += core gui qml quick quickcontrols2 testlib
CONFIG += c++17 testcase link_pkgconfig
PKGCONFIG += libpipewire-0.3 alsa
TARGET = holophonor_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += \
    ../src/audioengine.h \
    ../src/backend.h \
    ../src/control.h \
    ../src/dsp.h \
    ../src/inkpainter.h \
    ../src/instrumentmodel.h \
    ../src/instruments.h \
    ../src/midiinput.h \
    ../src/paper.h \
    ../src/reverb.h \
    ../src/synth.h \
    ../src/theme.h

SOURCES += \
    holophonor_tests.cpp \
    ../src/audioengine.cpp \
    ../src/backend.cpp \
    ../src/inkpainter.cpp \
    ../src/instrumentmodel.cpp \
    ../src/instruments.cpp \
    ../src/midiinput.cpp \
    ../src/paper.cpp \
    ../src/reverb.cpp \
    ../src/synth.cpp \
    ../src/theme.cpp
