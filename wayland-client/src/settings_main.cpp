// Settings window for Unikey-Wayland. It runs only while it is open, so the
// input method itself never loads the widget toolkit.
#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include "mainwindow.h"

static const char* kSettingsService = "io.github.ubuntu2310fake.UnikeyWayland.Settings";

int main(int argc, char** argv) {
    setenv("QT_QPA_PLATFORM", "wayland;xcb", 0); // Prefer Wayland, fallback to xcb.
    QApplication app(argc, argv);
    app.setDesktopFileName("io.github.ubuntu2310fake.UnikeyWayland");
    const bool showExclude = app.arguments().contains("--exclude");

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected() && !bus.registerService(kSettingsService)) {
        // One window is enough: bring the open one forward instead.
        bus.call(QDBusMessage::createMethodCall(kSettingsService, "/Settings",
                                                kSettingsService, "Raise"));
        return 0;
    }

    MainWindow window;
    bus.registerObject("/Settings", &window, QDBusConnection::ExportScriptableSlots);
    if (showExclude) window.selectTab("Danh sách loại trừ");
    window.show();
    return app.exec();
}
