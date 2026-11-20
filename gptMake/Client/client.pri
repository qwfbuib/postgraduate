QT += widgets network multimedia multimediawidgets concurrent
CONFIG += c++17

INCLUDEPATH += $$PWD $$PWD/../Server

SOURCES += $$PWD/clientconnection.cpp \
    $$PWD/weather.cpp \
    $$PWD/weathertransport.cpp \
    $$PWD/display.cpp \
    $$PWD/nativevideo.cpp \
    $$PWD/clientwindow.cpp \
    $$PWD/../Server/legacyprotocol.cpp \
    $$PWD/../Server/uploadreceiver.cpp

HEADERS += $$PWD/clientconnection.h \
    $$PWD/weather.h \
    $$PWD/weathertransport.h \
    $$PWD/display.h \
    $$PWD/nativevideo.h \
    $$PWD/clientwindow.h \
    $$PWD/../Server/legacyprotocol.h \
    $$PWD/../Server/uploadreceiver.h

win32:LIBS += -lwinhttp -lmfplat -lmfuuid -lole32
