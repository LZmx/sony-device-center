#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>

#include "DeviceCenterController.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("Sony Device Center");
    app.setOrganizationName("SonyBridge");
    app.setApplicationVersion(SONY_DEVICE_CENTER_VERSION);

    // Wayland and the GNOME/KDE shells match a window to its .desktop entry by
    // this name; without it the taskbar falls back to a generic placeholder
    // even though the window icon below is set.
    QGuiApplication::setDesktopFileName("sony-device-center");

    // The raster form is used deliberately: QIcon can only read the SVG brand
    // asset when Qt's qsvg image plugin is deployed alongside the binary.
    app.setWindowIcon(QIcon(":/resources/brand/app-icon.png"));

    sony::devicecenter::DeviceCenterController controller;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("controller", &controller);
    engine.loadFromModule("SonyDeviceCenter", "Main");

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
