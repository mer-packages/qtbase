TARGET = qconnmanbearer

PLUGIN_TYPE = bearer
PLUGIN_CLASS_NAME = QConnmanEnginePlugin
load(qt_plugin)

QT = core network-private dbus

HEADERS += \
           qconnmanengine.h \
           ../qnetworksession_impl.h \
           ../qbearerengine_impl.h

SOURCES += main.cpp \
           qconnmanengine.cpp \
           ../qnetworksession_impl.cpp

OTHER_FILES += connman.json
CONFIG += link_pkgconfig
PKGCONFIG += connman-qt5
PKGCONFIG += qofono-qt5
