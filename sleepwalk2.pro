QT -= gui
QT *= dbus network

TEMPLATE = app
TARGET   = sleepwalk2

CONFIG += c++2a console link_pkgconfig

PKGCONFIG *= fmt

SOURCES += \
        $$files(src/*.C)

HEADERS += \
    $$files(src/*.H)
