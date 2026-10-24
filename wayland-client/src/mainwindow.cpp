#include "mainwindow.h"
// Constants previously defined in keycons.h
#define TELEX_INPUT 1
#define VNI_INPUT 2
#define VIQR_INPUT 3
#define TELEX_SIMPLIFIED_INPUT 4

#define UNICODE_CHARSET 1
#define TCVN3_CHARSET 2
#define VNI_CHARSET 3
#define VIQR_CHARSET 4
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QPlainTextEdit>
#include <QDBusMessage>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusServiceWatcher>
#include <QCoreApplication>
#include <QProcess>
#include <QStandardPaths>

MainWindow::MainWindow(bool is_gnome, QWidget *parent)
    : QWidget(parent) {
    setWindowTitle("Unikey-Wayland");
    setFixedSize(450, 360);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    m_tabWidget = new QTabWidget(this);
    mainLayout->addWidget(m_tabWidget);

    // --- Tab Cơ bản ---
    QWidget* tabBasic = new QWidget();
    QVBoxLayout* basicLayout = new QVBoxLayout(tabBasic);

    QGridLayout* comboLayout = new QGridLayout();
    comboLayout->addWidget(new QLabel("Bảng mã:"), 0, 0);
    m_charsetCombo = new QComboBox();
    m_charsetCombo->addItem("Unicode", QVariant(UNICODE_CHARSET));
    m_charsetCombo->addItem("TCVN3 (ABC)", QVariant(TCVN3_CHARSET));
    m_charsetCombo->addItem("VNI Windows", QVariant(VNI_CHARSET));
    m_charsetCombo->addItem("VIQR", QVariant(VIQR_CHARSET));
    comboLayout->addWidget(m_charsetCombo, 0, 1);

    comboLayout->addWidget(new QLabel("Kiểu gõ:"), 1, 0);
    m_methodCombo = new QComboBox();
    m_methodCombo->addItem("Telex", QVariant(TELEX_INPUT));
    m_methodCombo->addItem("Telex giản lược", QVariant(TELEX_SIMPLIFIED_INPUT));
    m_methodCombo->addItem("VNI", QVariant(VNI_INPUT));
    m_methodCombo->addItem("VIQR", QVariant(VIQR_INPUT));
    comboLayout->addWidget(m_methodCombo, 1, 1);
    basicLayout->addLayout(comboLayout);

    QFrame* line1 = new QFrame();
    line1->setFrameShape(QFrame::HLine);
    line1->setFrameShadow(QFrame::Sunken);
    basicLayout->addWidget(line1);

    m_freeMarkingCheck = new QCheckBox("Cho phép gõ tự do (free marking)");
    m_freeMarkingCheck->setChecked(true);
    m_modernStyleCheck = new QCheckBox("Đặt dấu oà, uý (thay vì òa, úy)");
    m_autoRestoreCheck = new QCheckBox("Tự động khôi phục phím với từ sai");
    m_spellCheckCheck = new QCheckBox("Kiểm tra chính tả");
    m_macroCheck = new QCheckBox("Cho phép gõ tắt");

    basicLayout->addWidget(m_freeMarkingCheck);
    basicLayout->addWidget(m_modernStyleCheck);
    basicLayout->addWidget(m_autoRestoreCheck);
    basicLayout->addWidget(m_spellCheckCheck);
    basicLayout->addWidget(m_macroCheck);
    basicLayout->addStretch();

    m_tabWidget->addTab(tabBasic, "Cơ bản");

    // --- Tab Phím tắt ---
    m_evHotkeyCombo = new QComboBox();
    m_evHotkeyCombo->addItems({"Ctrl + Shift", "Alt + Z"});
    m_restoreHotkeyCombo = new QComboBox();
    m_restoreHotkeyCombo->addItems({"Shift + Backspace", "Alt + Backspace"});

    if (!is_gnome) {
        QWidget* tabHotkey = new QWidget();
        QVBoxLayout* hotkeyLayout = new QVBoxLayout(tabHotkey);
        
        QGridLayout* hkGrid = new QGridLayout();
        hkGrid->addWidget(new QLabel("Phím chuyển E/V:"), 0, 0);
        hkGrid->addWidget(m_evHotkeyCombo, 0, 1);

        hkGrid->addWidget(new QLabel("Phục hồi từ sai:"), 1, 0);
        hkGrid->addWidget(m_restoreHotkeyCombo, 1, 1);
        hotkeyLayout->addLayout(hkGrid);
        hotkeyLayout->addStretch();
        m_tabWidget->addTab(tabHotkey, "Phím tắt");
    }

    // --- Tab Hệ thống ---
    QWidget* tabSystem = new QWidget();
    QVBoxLayout* sysLayout = new QVBoxLayout(tabSystem);
    m_showOnStartupCheck = new QCheckBox("Bật hội thoại này khi khởi động");
    m_showOnStartupCheck->setChecked(true);
    sysLayout->addWidget(m_showOnStartupCheck);
    sysLayout->addWidget(new QLabel("<i>Các tuỳ chọn khởi động cùng hệ thống,\nchạy quyền admin đã được gỡ bỏ cho Wayland.</i>"));
    sysLayout->addStretch();
    if (!is_gnome) {
        m_tabWidget->addTab(tabSystem, "Hệ thống");
    }

    // --- Tab Gõ tắt ---
    QWidget* tabMacro = new QWidget();
    QVBoxLayout* macroLayout = new QVBoxLayout(tabMacro);
    m_macroWithConsonantCheck = new QCheckBox("Gõ tắt kể cả khi gõ tắt cùng phụ âm");
    m_macroTableBtn = new QPushButton("Bảng gõ tắt...");
    macroLayout->addWidget(m_macroWithConsonantCheck);
    macroLayout->addWidget(m_macroTableBtn);
    macroLayout->addStretch();
    m_tabWidget->addTab(tabMacro, "Gõ tắt");

    // --- Tab Danh sách loại trừ ---
    QWidget* tabExclude = new QWidget();
    QVBoxLayout* excludeLayout = new QVBoxLayout(tabExclude);
    QLabel* excludeLabel = new QLabel("Preedit đã tắt; danh sách cũ chỉ được giữ để tương thích:\n(Một dòng cho mỗi ứng dụng, ví dụ: kitty, studio, java)");
    m_preeditAppsTextEdit = new QPlainTextEdit(this);
    excludeLayout->addWidget(excludeLabel);
    excludeLayout->addWidget(m_preeditAppsTextEdit);
    m_tabWidget->addTab(tabExclude, "Danh sách loại trừ");

    // --- Tab Thông tin ---
    QWidget* tabAbout = new QWidget();
    QVBoxLayout* aboutLayout = new QVBoxLayout(tabAbout);
    const char* session_type = getenv("XDG_SESSION_TYPE");
    QString title = "<h2>Unikey-Wayland</h2>";
    if (is_gnome) {
        if (session_type && QString(session_type).toLower() == "x11") {
            title = "<h2>Unikey (X11 Edition)</h2>";
        } else if (session_type && QString(session_type).toLower() == "wayland") {
            title = "<h2>Unikey (Wayland Edition)</h2>";
        } else {
            title = "<h2>Unikey (GNOME Edition)</h2>";
        }
    }
    QLabel* titleLabel = new QLabel(title);
    titleLabel->setAlignment(Qt::AlignCenter);
    QString versionStr = QString("2.0.2");
#ifdef UKW_VERSION
    versionStr = QString(UKW_VERSION);
#endif
    QLabel* infoLabel = new QLabel(is_gnome ? "Khởi nguồn từ mã nguồn Unikey nhưng logic sử dụng Bamboo Engine<br>Chạy dưới chế độ IBus Engine<br>Phiên bản: " + versionStr : "Khởi nguồn từ mã nguồn Unikey nhưng logic sử dụng Bamboo Engine<br>Viết lại UI bằng Qt 6 cho KDE Plasma Wayland<br>Phiên bản: " + versionStr);
    infoLabel->setAlignment(Qt::AlignCenter);
    aboutLayout->addStretch();
    aboutLayout->addWidget(titleLabel);
    aboutLayout->addWidget(infoLabel);
    aboutLayout->addStretch();
    m_tabWidget->addTab(tabAbout, "Thông tin");

    // --- Bottom Buttons ---
    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_defaultBtn = new QPushButton("Mặc định");
    m_closeBtn = new QPushButton("Đóng");
    m_inputMethodBtn = new QPushButton();
    btnLayout->addWidget(m_defaultBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_inputMethodBtn);
    btnLayout->addWidget(m_closeBtn);
    mainLayout->addLayout(btnLayout);

    // Connections
    connect(m_charsetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::applySettings);
    connect(m_methodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::applySettings);
    connect(m_freeMarkingCheck, &QCheckBox::stateChanged, this, &MainWindow::applySettings);
    connect(m_modernStyleCheck, &QCheckBox::stateChanged, this, &MainWindow::applySettings);
    connect(m_autoRestoreCheck, &QCheckBox::stateChanged, this, &MainWindow::applySettings);
    connect(m_spellCheckCheck, &QCheckBox::stateChanged, this, &MainWindow::applySettings);
    connect(m_macroCheck, &QCheckBox::stateChanged, this, &MainWindow::applySettings);
    connect(m_evHotkeyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::applySettings);
    connect(m_preeditAppsTextEdit, &QPlainTextEdit::textChanged, this, &MainWindow::applySettings);
    
    connect(m_macroTableBtn, &QPushButton::clicked, this, &MainWindow::onMacroButtonClicked);
    connect(m_closeBtn, &QPushButton::clicked, this, &MainWindow::onCloseClicked);
    connect(m_inputMethodBtn, &QPushButton::clicked, this, &MainWindow::onInputMethodButtonClicked);

    // The button follows the input method as it quits or starts again.
    m_inputMethodWatcher = new QDBusServiceWatcher(
        UK_DBUS_SERVICE, QDBusConnection::sessionBus(),
        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(m_inputMethodWatcher, &QDBusServiceWatcher::serviceRegistered,
            this, &MainWindow::updateInputMethodButton);
    connect(m_inputMethodWatcher, &QDBusServiceWatcher::serviceUnregistered,
            this, &MainWindow::updateInputMethodButton);
    updateInputMethodButton();

    // Load persisted configuration
    loadConfig();
}

void MainWindow::applySettings() {
    if (m_loadingConfig) return;
    saveConfig();
    QDBusConnection::sessionBus().send(QDBusMessage::createMethodCall(
        UK_DBUS_SERVICE, UK_CONTROL_PATH, UK_CONTROL_INTERFACE, "ReloadConfig"));
}

bool MainWindow::inputMethodRunning() const {
    QDBusConnectionInterface* bus = QDBusConnection::sessionBus().interface();
    return bus && bus->isServiceRegistered(UK_DBUS_SERVICE);
}

void MainWindow::updateInputMethodButton() {
    m_inputMethodBtn->setText(inputMethodRunning() ? "Thoát bộ gõ" : "Bật bộ gõ");
    m_inputMethodBtn->setEnabled(true);
}

void MainWindow::onInputMethodButtonClicked() {
    m_inputMethodBtn->setEnabled(false);
    if (inputMethodRunning()) {
        QDBusConnection::sessionBus().send(QDBusMessage::createMethodCall(
            UK_DBUS_SERVICE, UK_CONTROL_PATH, UK_CONTROL_INTERFACE, "Quit"));
        return;
    }
    // Started without KWin's socket, the input method asks KWin to start it.
    QString program = QCoreApplication::applicationDirPath() + "/unikey-wayland";
    if (!QFileInfo::exists(program)) program = QStandardPaths::findExecutable("unikey-wayland");
    if (program.isEmpty() || !QProcess::startDetached(program, {})) {
        updateInputMethodButton();
    }
}

void MainWindow::Raise() {
    show();
    raise();
    activateWindow();
}

void MainWindow::onMacroButtonClicked() {
    MacroDialog dialog(this);
    dialog.setMacroTable(m_macros);
    if (dialog.exec() == QDialog::Accepted) {
        m_macros = dialog.getMacroTable();
        applySettings();
    }
}

void MainWindow::onCloseClicked() {
    close();
}

void MainWindow::loadConfig() {
    m_loadingConfig = true;

    const UkConfig config = uk_load_config();
    int methodIdx = m_methodCombo->findData(config.method);
    if (methodIdx != -1) m_methodCombo->setCurrentIndex(methodIdx);
    int charsetIdx = m_charsetCombo->findData(config.charset);
    if (charsetIdx != -1) m_charsetCombo->setCurrentIndex(charsetIdx);
    m_freeMarkingCheck->setChecked(config.freeMarking);
    m_modernStyleCheck->setChecked(config.modernStyle);
    m_autoRestoreCheck->setChecked(config.autoRestore);
    m_spellCheckCheck->setChecked(config.spellCheck);
    m_macroCheck->setChecked(config.macroEnabled);
    m_showOnStartupCheck->setChecked(config.showOnStartup);
    m_macroWithConsonantCheck->setChecked(config.macroWithConsonant);
    m_evHotkeyCombo->setCurrentIndex(config.switchKey);
    m_macros = config.macros;

    // Load preedit apps list
    QString preeditPath = QDir::homePath() + "/UnikeyWayland/preedit_apps.txt";
    QFile preeditFile(preeditPath);
    if (preeditFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_preeditAppsTextEdit->setPlainText(QString::fromUtf8(preeditFile.readAll()));
        preeditFile.close();
    } else {
        // Default list
        QString defaults;
        m_preeditAppsTextEdit->setPlainText(defaults);
        QDir().mkpath(QFileInfo(preeditPath).absolutePath());
        if (preeditFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            preeditFile.write(defaults.toUtf8());
            preeditFile.close();
        }
    }

    m_loadingConfig = false;
}

void MainWindow::saveConfig() {
    if (m_loadingConfig) return;

    UkConfig config;
    config.method = m_methodCombo->currentData().toInt();
    config.charset = m_charsetCombo->currentData().toInt();
    config.freeMarking = m_freeMarkingCheck->isChecked();
    config.modernStyle = m_modernStyleCheck->isChecked();
    config.autoRestore = m_autoRestoreCheck->isChecked();
    config.spellCheck = m_spellCheckCheck->isChecked();
    config.macroEnabled = m_macroCheck->isChecked();
    config.showOnStartup = m_showOnStartupCheck->isChecked();
    config.macroWithConsonant = m_macroWithConsonantCheck->isChecked();
    config.switchKey = m_evHotkeyCombo->currentIndex();
    config.macros = m_macros;
    uk_save_options(config);

    // Save preedit apps list
    QString preeditPath = QDir::homePath() + "/UnikeyWayland/preedit_apps.txt";
    QFile preeditFile(preeditPath);
    if (preeditFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        preeditFile.write(m_preeditAppsTextEdit->toPlainText().toUtf8());
        preeditFile.close();
    }
}

void MainWindow::selectTab(const QString& name) {
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        if (m_tabWidget->tabText(i) == name) {
            m_tabWidget->setCurrentIndex(i);
            break;
        }
    }
}
