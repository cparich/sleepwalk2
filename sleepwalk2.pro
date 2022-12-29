QT -= gui
QT *= dbus network

TARGET = sleepwalk2

CONFIG += c++2a console link_pkgconfig

PKGCONFIG *= fmt

SOURCES += \
        $$files(src/*.C)

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

HEADERS += \
    $$files(src/*.H)
