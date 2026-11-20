TEMPLATE = app
TARGET = client-tests
QT += testlib
CONFIG += testcase console

include(../client.pri)

SOURCES += client-tests.cpp \
    testpreview.cpp \
    ../../Server/mainwindow.cpp \
    ../../Server/actionbutton.cpp \
    ../../Server/addresslist.cpp \
    ../../Server/terminalserver.cpp \
    ../../Server/filequeue.cpp

HEADERS += testpreview.h \
    ../../Server/mainwindow.h \
    ../../Server/actionbutton.h \
    ../../Server/addresslist.h \
    ../../Server/terminalserver.h \
    ../../Server/filequeue.h
