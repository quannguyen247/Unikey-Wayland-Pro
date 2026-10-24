#ifndef CONTROL_H
#define CONTROL_H

#include <QDBusAbstractAdaptor>
#include <QTimer>
#include <functional>
#include "config.h"

// Lets the settings window and the launcher reach the running input method,
// which now lives in a separate process.
class ControlAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", UK_CONTROL_INTERFACE)
    Q_PROPERTY(bool VietMode READ vietMode)

public:
    struct Actions {
        std::function<bool()> vietMode;
        std::function<void()> reloadConfig;
        std::function<void()> showSettings;
        std::function<void()> quit;
    };

    ControlAdaptor(QObject* parent, Actions actions)
        : QDBusAbstractAdaptor(parent), m_actions(std::move(actions)) {}

    bool vietMode() const { return m_actions.vietMode && m_actions.vietMode(); }

public slots:
    void ReloadConfig() { if (m_actions.reloadConfig) m_actions.reloadConfig(); }
    void ShowSettings() { if (m_actions.showSettings) m_actions.showSettings(); }
    // Reply first; exiting inside the call would leave the caller waiting.
    void Quit() { if (m_actions.quit) QTimer::singleShot(0, this, m_actions.quit); }

private:
    Actions m_actions;
};

#endif // CONTROL_H
