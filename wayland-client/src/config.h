#ifndef UK_CONFIG_H
#define UK_CONFIG_H

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <map>
#include <string>

// Where the running input method answers on the session bus.
#define UK_DBUS_SERVICE "io.github.ubuntu2310fake.UnikeyWayland"
#define UK_CONTROL_PATH "/Control"
#define UK_CONTROL_INTERFACE "io.github.ubuntu2310fake.UnikeyWayland.Control"

// The input method and the settings window run as separate processes and
// share only this file, so each rewrites its own keys on top of what is on
// disk: the window owns the options, the input method owns vietKey.
struct UkConfig {
    int method = 1;  // Telex
    int charset = 1; // Unicode
    bool freeMarking = true;
    bool modernStyle = false;
    bool autoRestore = false;
    bool spellCheck = false;
    bool macroEnabled = false;
    bool macroWithConsonant = false;
    bool showOnStartup = true;
    int switchKey = 0;
    bool vietKey = true;
    std::map<std::string, std::string> macros;
};

inline QString uk_config_dir() {
    return QDir::homePath() + "/UnikeyWayland";
}

inline QString uk_config_path() {
    return uk_config_dir() + "/global.json";
}

inline QJsonObject uk_read_config_object() {
    QFile file(uk_config_path());
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

inline UkConfig uk_config_from_object(const QJsonObject& obj) {
    UkConfig config;
    // 0 was never a valid method or charset; keep the defaults for it.
    if (const int method = obj.value("method").toInt(0); method >= 1 && method <= 4) {
        config.method = method;
    }
    if (const int charset = obj.value("charset").toInt(0); charset >= 1 && charset <= 4) {
        config.charset = charset;
    }
    config.freeMarking = obj.value("freeMarking").toBool(true);
    config.modernStyle = obj.value("modernStyle").toBool(false);
    config.autoRestore = obj.value("autoRestore").toBool(false);
    config.spellCheck = obj.value("spellCheck").toBool(false);
    config.macroEnabled = obj.value("macroEnabled").toBool(false);
    config.macroWithConsonant = obj.value("macroWithConsonant").toBool(false);
    config.showOnStartup = obj.value("showOnStartup").toBool(true);
    config.switchKey = obj.value("switchKey").toInt(0) == 1 ? 1 : 0;
    config.vietKey = obj.value("vietKey").toBool(true);
    const QJsonObject macros = obj.value("macros").toObject();
    for (auto it = macros.begin(); it != macros.end(); ++it) {
        config.macros[it.key().toStdString()] = it.value().toString().toStdString();
    }
    return config;
}

inline UkConfig uk_load_config() {
    return uk_config_from_object(uk_read_config_object());
}

inline bool uk_write_config_object(const QJsonObject& obj) {
    QDir().mkpath(uk_config_dir());
    QSaveFile file(uk_config_path());
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(obj).toJson());
    return file.commit();
}

// Saves the settings window's options and keeps vietKey as it is on disk.
inline bool uk_save_options(const UkConfig& config) {
    QJsonObject obj = uk_read_config_object();
    obj["method"] = config.method;
    obj["charset"] = config.charset;
    obj["freeMarking"] = config.freeMarking;
    obj["modernStyle"] = config.modernStyle;
    obj["autoRestore"] = config.autoRestore;
    obj["spellCheck"] = config.spellCheck;
    obj["macroEnabled"] = config.macroEnabled;
    obj["macroWithConsonant"] = config.macroWithConsonant;
    obj["showOnStartup"] = config.showOnStartup;
    obj["switchKey"] = config.switchKey;
    if (!obj.contains("vietKey")) obj["vietKey"] = config.vietKey;
    QJsonObject macros;
    for (const auto& [key, value] : config.macros) {
        macros[QString::fromStdString(key)] = QString::fromStdString(value);
    }
    obj["macros"] = macros;
    return uk_write_config_object(obj);
}

inline bool uk_save_viet_mode(bool viet) {
    QJsonObject obj = uk_read_config_object();
    obj["vietKey"] = viet;
    return uk_write_config_object(obj);
}

#endif // UK_CONFIG_H
