QT += widgets network
CONFIG += c++17
TEMPLATE = app
TARGET = TCPServer

include(server-core.pri)

SOURCES += main.cpp \
    mainwindow.cpp \
    actionbutton.cpp \
    addresslist.cpp
HEADERS += mainwindow.h \
    actionbutton.h \
    addresslist.h
