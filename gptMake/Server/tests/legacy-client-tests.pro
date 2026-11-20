QT += widgets testlib
CONFIG += c++17 testcase console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = legacy-client-tests

include(../server-core.pri)

LEGACY_CLIENT = $$clean_path($$PWD/../../../TCPClient)
INCLUDEPATH += $$LEGACY_CLIENT
SOURCES += legacy-client-tests.cpp $$LEGACY_CLIENT/mainwindow.cpp
HEADERS += $$LEGACY_CLIENT/mainwindow.h
FORMS += $$LEGACY_CLIENT/mainwindow.ui
RESOURCES += $$LEGACY_CLIENT/res.qrc
