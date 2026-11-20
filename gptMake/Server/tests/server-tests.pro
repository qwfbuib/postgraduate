QT += widgets testlib
CONFIG += c++17 testcase console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = server-tests

include(../server-core.pri)
SOURCES += server-tests.cpp \
    ../mainwindow.cpp \
    ../actionbutton.cpp \
    ../addresslist.cpp
HEADERS += ../mainwindow.h \
    ../actionbutton.h \
    ../addresslist.h
