#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QWidget>
#include <QTabWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include "macrodialog.h"
#include "config.h"

class QDBusServiceWatcher;

// The settings window runs as its own program; it saves the options and asks
// the running input method to reload them over D-Bus.
class MainWindow : public QWidget {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.ubuntu2310fake.UnikeyWayland.Settings")
public:
    explicit MainWindow(bool is_gnome = false, QWidget *parent = nullptr);

public slots:
    // A second launch asks this window to come forward.
    Q_SCRIPTABLE void Raise();

private slots:
    void applySettings();
    void onCloseClicked();
    void onMacroButtonClicked();
    void onInputMethodButtonClicked();
    void updateInputMethodButton();

private:
    QTabWidget* m_tabWidget;

    // Cơ bản
    QComboBox* m_charsetCombo;
    QComboBox* m_methodCombo;
    QCheckBox* m_freeMarkingCheck;
    QCheckBox* m_modernStyleCheck;
    QCheckBox* m_macroCheck;
    QCheckBox* m_autoRestoreCheck;
    QCheckBox* m_spellCheckCheck;

    // Phím tắt
    QComboBox* m_evHotkeyCombo;
    QComboBox* m_restoreHotkeyCombo;

    // Hệ thống
    QCheckBox* m_showOnStartupCheck;

    // Gõ tắt
    QCheckBox* m_macroWithConsonantCheck;
    QPushButton* m_macroTableBtn;

    QPushButton* m_closeBtn;
    QPushButton* m_defaultBtn;
    QPushButton* m_inputMethodBtn;
    QDBusServiceWatcher* m_inputMethodWatcher;

    // Config persistence
    void loadConfig();
    void saveConfig();
    bool inputMethodRunning() const;
    bool m_loadingConfig = false;
    std::map<std::string, std::string> m_macros;

public:
    // Preedit exclude/include apps list
    class QPlainTextEdit* m_preeditAppsTextEdit;

public:
    void selectTab(const QString& name);
};

#endif // MAINWINDOW_H
