#include "windowtracker.h"
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusConnection>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QStandardPaths>
#include <QTimer>
#include <iostream>
#include <fstream>

WindowTracker::WindowTracker(QObject *parent) : QObject(parent) {
    loadExcludedApps();

    // Register DBus object
    new WindowTrackerAdaptor(this);
    if (!QDBusConnection::sessionBus().registerService("io.github.ubuntu2310fake.UnikeyWayland")) {
        qWarning() << "Failed to register io.github.ubuntu2310fake.UnikeyWayland DBus service:" << QDBusConnection::sessionBus().lastError().message();
    }
    if (!QDBusConnection::sessionBus().registerObject("/WindowTracker", this)) {
        qWarning() << "Failed to register /WindowTracker object";
    }
}

void WindowTracker::loadExcludedApps() {
    m_excludedApps.clear();
    QString configPath = QDir::homePath() + "/UnikeyWayland/preedit_apps.txt";

    QFile file(configPath);
    if (!file.exists()) {
        QDir().mkpath(QFileInfo(configPath).absolutePath());
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            file.close();
        }
    }

    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (!line.isEmpty() && !line.startsWith("#")) {
                m_excludedApps.push_back(line.toLower().toStdString());
            }
        }
        file.close();
    }
}

void WindowTracker::reloadExcludedApps() {
    loadExcludedApps();
    // Re-evaluate current active window
    emit activeWindowChangedSignal(QString::fromStdString(m_activeWindowClass));
}

bool WindowTracker::isAppExcluded(const std::string& appClass) const {
    if (appClass.empty()) return false;
    std::string lowerClass;
    for (char c : appClass) {
        lowerClass += std::tolower(c);
    }
    
    for (const auto& excluded : m_excludedApps) {
        if (lowerClass.find(excluded) != std::string::npos) {
            return true;
        }
    }
    return false;
}

static bool debug_enabled() {
    static const bool enabled = [] {
        const QByteArray value = qgetenv("UNIKEY_WAYLAND_DEBUG");
        return !value.isEmpty() && value != "0";
    }();
    return enabled;
}

void WindowTracker::activeWindowChanged(const QString& windowClass) {
    m_activeWindowClass = windowClass.toStdString();
    if (debug_enabled()) {
        QFile f(QDir::tempPath() + "/tracker.log");
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&f);
            out << "DEBUG: WindowTracker received: " << windowClass << "\n";
        }
    }

    emit activeWindowChangedSignal(windowClass);
}

static const QString kScriptName = QStringLiteral("unikey-wayland-tracker");
// Earlier versions loaded this file under its path. That script connected one
// more captionChanged handler on every activation and reported background
// windows as active, so each title change fanned out into hundreds of calls.
static const QString kLegacyScriptName = QStringLiteral("/tmp/unikey_wayland_tracker.js");

void WindowTracker::injectKWinScript() {
    // Inject script via KWin DBus
    QDBusInterface kwinScripting("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", QDBusConnection::sessionBus());
    if (!kwinScripting.isValid()) {
        qWarning() << "Could not connect to org.kde.KWin /Scripting. Are you running KDE Plasma Wayland?";
        return;
    }

    // KWin keeps a script until it is unloaded, including one from a previous run.
    kwinScripting.call("unloadScript", kLegacyScriptName);
    kwinScripting.call("unloadScript", kScriptName);

    // Report activations only; a title change does not change the input target.
    QString scriptCode =
        "function notifyActiveWindow(window) {\n"
        "    var appId = '';\n"
        "    var title = '';\n"
        "    var pid = 0;\n"
        "    if (window) {\n"
        "        appId = (window.resourceClass ? window.resourceClass : (window.desktopFileName ? window.desktopFileName : '')).toString();\n"
        "        title = (window.caption ? window.caption : '').toString();\n"
        "        pid = window.pid ? window.pid : 0;\n"
        "    }\n"
        "    callDBus('io.github.ubuntu2310fake.UnikeyWayland', '/WindowTracker', 'io.github.ubuntu2310fake.UnikeyWayland.WindowTracker', 'activeWindowChanged', window ? appId + '|||' + title + '|||' + pid : '');\n"
        "}\n"
        "if (workspace.windowActivated) {\n"
        "    workspace.windowActivated.connect(notifyActiveWindow);\n"
        "} else {\n"
        "    workspace.clientActivated.connect(notifyActiveWindow);\n"
        "}\n"
        "notifyActiveWindow(workspace.activeWindow !== undefined ? workspace.activeWindow : workspace.activeClient);\n";

    // A per-user runtime directory, so another account cannot plant the script.
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty()) dir = QDir::tempPath();
    m_scriptPath = dir + "/unikey-wayland-tracker.js";
    QFile tmpFile(m_scriptPath);
    if (!tmpFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning() << "Could not write the KWin tracker script" << m_scriptPath;
        return;
    }
    QTextStream(&tmpFile) << scriptCode;
    tmpFile.close();

    loadKWinScript(0);
}

void WindowTracker::loadKWinScript(int attempt) {
    QDBusInterface kwinScripting("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", QDBusConnection::sessionBus());
    QDBusReply<int> reply = kwinScripting.call("loadScript", m_scriptPath, kScriptName);
    if (!reply.isValid()) {
        qWarning() << "Failed to load KWin script:" << reply.error().message();
        return;
    }
    if (reply.value() < 0) {
        // KWin deletes an unloaded script later, and refuses a duplicate name until then.
        if (attempt < 20) {
            QTimer::singleShot(100, this, [this, attempt]() { loadKWinScript(attempt + 1); });
        } else {
            qWarning() << "KWin kept the previous tracker script loaded";
        }
        return;
    }
    QString scriptPath = QString("/Scripting/Script%1").arg(reply.value());
    QDBusInterface scriptObj("org.kde.KWin", scriptPath, "org.kde.kwin.Script", QDBusConnection::sessionBus());
    scriptObj.call("run");
    if (debug_enabled()) {
        qDebug() << "Successfully loaded and started KWin window tracker script at" << scriptPath;
    }
}
