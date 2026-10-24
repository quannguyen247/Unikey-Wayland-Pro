#include "trayicon.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusServiceWatcher>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>

namespace {

enum MenuId {
    RootItem = 0,
    ToggleItem = 1,
    SettingsItem = 2,
    SeparatorItem = 3,
    QuitItem = 4,
};

const QString kWatcherService = QStringLiteral("org.kde.StatusNotifierWatcher");

QVariantMap filtered(const QVariantMap& properties, const QStringList& names) {
    if (names.isEmpty()) return properties;
    QVariantMap result;
    for (const QString& name : names) {
        if (properties.contains(name)) result.insert(name, properties.value(name));
    }
    return result;
}

DBusMenuLayoutItem layoutFor(const TrayIcon* tray, int id, int depth, const QStringList& names) {
    DBusMenuLayoutItem item;
    item.id = id;
    item.properties = filtered(tray->itemProperties(id), names);
    if (depth != 0) {
        for (int child : tray->childIds(id)) {
            item.children.append(layoutFor(tray, child, depth < 0 ? depth : depth - 1, names));
        }
    }
    return item;
}

} // namespace

QDBusArgument& operator<<(QDBusArgument& argument, const DBusMenuItem& item) {
    argument.beginStructure();
    argument << item.id << item.properties;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, DBusMenuItem& item) {
    argument.beginStructure();
    argument >> item.id >> item.properties;
    argument.endStructure();
    return argument;
}

QDBusArgument& operator<<(QDBusArgument& argument, const DBusMenuItemKeys& item) {
    argument.beginStructure();
    argument << item.id << item.properties;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, DBusMenuItemKeys& item) {
    argument.beginStructure();
    argument >> item.id >> item.properties;
    argument.endStructure();
    return argument;
}

QDBusArgument& operator<<(QDBusArgument& argument, const DBusMenuLayoutItem& item) {
    argument.beginStructure();
    argument << item.id << item.properties;
    argument.beginArray(qMetaTypeId<QDBusVariant>());
    for (const DBusMenuLayoutItem& child : item.children) {
        argument << QDBusVariant(QVariant::fromValue(child));
    }
    argument.endArray();
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, DBusMenuLayoutItem& item) {
    argument.beginStructure();
    argument >> item.id >> item.properties;
    item.children.clear();
    argument.beginArray();
    while (!argument.atEnd()) {
        QDBusVariant variant;
        argument >> variant;
        DBusMenuLayoutItem child;
        variant.variant().value<QDBusArgument>() >> child;
        item.children.append(child);
    }
    argument.endArray();
    argument.endStructure();
    return argument;
}

QDBusArgument& operator<<(QDBusArgument& argument, const DBusMenuEvent& event) {
    argument.beginStructure();
    argument << event.id << event.eventId << event.data << event.timestamp;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, DBusMenuEvent& event) {
    argument.beginStructure();
    argument >> event.id >> event.eventId >> event.data >> event.timestamp;
    argument.endStructure();
    return argument;
}

StatusNotifierItemAdaptor::StatusNotifierItemAdaptor(TrayIcon* parent)
    : QDBusAbstractAdaptor(parent) {
}

QString StatusNotifierItemAdaptor::iconName() const {
    const TrayIcon* tray = static_cast<const TrayIcon*>(parent());
    return tray->isVietMode() ? "unikey-wayland-v" : "unikey-wayland-e";
}

QString StatusNotifierItemAdaptor::iconThemePath() const {
    // Icons installed under ~/.local/share are not in every host's theme path.
    const QString icon = QStandardPaths::locate(
        QStandardPaths::GenericDataLocation,
        "icons/hicolor/scalable/apps/unikey-wayland-v.svg");
    if (icon.isEmpty()) return {};
    // .../icons/hicolor/scalable/apps/unikey-wayland-v.svg -> .../icons
    return QDir::cleanPath(QFileInfo(icon).absolutePath() + "/../../..");
}

void StatusNotifierItemAdaptor::Activate(int x, int y) {
    Q_UNUSED(x);
    Q_UNUSED(y);
    static_cast<TrayIcon*>(parent())->toggleMode();
}

void StatusNotifierItemAdaptor::SecondaryActivate(int x, int y) {
    Q_UNUSED(x);
    Q_UNUSED(y);
    static_cast<TrayIcon*>(parent())->showSettings();
}

void StatusNotifierItemAdaptor::ContextMenu(int x, int y) {
    // Hosts that read the Menu property show it themselves.
    Q_UNUSED(x);
    Q_UNUSED(y);
    static_cast<TrayIcon*>(parent())->showSettings();
}

DBusMenuAdaptor::DBusMenuAdaptor(QObject* object, TrayIcon* tray)
    : QDBusAbstractAdaptor(object), m_tray(tray) {
}

uint DBusMenuAdaptor::GetLayout(int parentId, int recursionDepth,
                                const QStringList& propertyNames,
                                DBusMenuLayoutItem& layout) {
    layout = layoutFor(m_tray, m_tray->hasItem(parentId) ? parentId : RootItem,
                       recursionDepth, propertyNames);
    return 1;
}

DBusMenuItemList DBusMenuAdaptor::GetGroupProperties(const QList<int>& ids,
                                                     const QStringList& propertyNames) {
    DBusMenuItemList result;
    for (int id : ids) {
        if (m_tray->hasItem(id)) {
            result.append({id, filtered(m_tray->itemProperties(id), propertyNames)});
        }
    }
    return result;
}

QDBusVariant DBusMenuAdaptor::GetProperty(int id, const QString& name) {
    return QDBusVariant(m_tray->itemProperties(id).value(name));
}

void DBusMenuAdaptor::Event(int id, const QString& eventId, const QDBusVariant& data,
                            uint timestamp) {
    Q_UNUSED(data);
    Q_UNUSED(timestamp);
    if (eventId == "clicked") m_tray->triggerItem(id);
}

QList<int> DBusMenuAdaptor::EventGroup(const DBusMenuEventList& events) {
    QList<int> idErrors;
    for (const DBusMenuEvent& event : events) {
        if (!m_tray->hasItem(event.id)) {
            idErrors.append(event.id);
        } else if (event.eventId == "clicked") {
            m_tray->triggerItem(event.id);
        }
    }
    return idErrors;
}

bool DBusMenuAdaptor::AboutToShow(int id) {
    Q_UNUSED(id);
    return false;
}

QList<int> DBusMenuAdaptor::AboutToShowGroup(const QList<int>& ids, QList<int>& idErrors) {
    for (int id : ids) {
        if (!m_tray->hasItem(id)) idErrors.append(id);
    }
    return {};
}

TrayIcon::TrayIcon(bool vietMode, Actions actions, QObject* parent)
    : QObject(parent), m_vietMode(vietMode), m_actions(std::move(actions)) {
    qDBusRegisterMetaType<DBusMenuItem>();
    qDBusRegisterMetaType<DBusMenuItemList>();
    qDBusRegisterMetaType<DBusMenuItemKeys>();
    qDBusRegisterMetaType<DBusMenuItemKeysList>();
    qDBusRegisterMetaType<DBusMenuLayoutItem>();
    qDBusRegisterMetaType<DBusMenuEvent>();
    qDBusRegisterMetaType<DBusMenuEventList>();

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return;

    m_itemAdaptor = new StatusNotifierItemAdaptor(this);
    bus.registerObject("/StatusNotifierItem", this, QDBusConnection::ExportAdaptors);
    QObject* menuObject = new QObject(this);
    m_menuAdaptor = new DBusMenuAdaptor(menuObject, this);
    bus.registerObject("/MenuBar", menuObject, QDBusConnection::ExportAdaptors);

    // Register again whenever the watcher or a tray host (re)starts.
    auto* watcher = new QDBusServiceWatcher(kWatcherService, bus,
                                            QDBusServiceWatcher::WatchForRegistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, &TrayIcon::registerWithWatcher);
    bus.connect(kWatcherService, "/StatusNotifierWatcher", kWatcherService,
                "StatusNotifierHostRegistered", this, SLOT(registerWithWatcher()));
    registerWithWatcher();
}

void TrayIcon::registerWithWatcher() {
    QDBusMessage msg = QDBusMessage::createMethodCall(
        kWatcherService, "/StatusNotifierWatcher", kWatcherService,
        "RegisterStatusNotifierItem");
    msg << "/StatusNotifierItem";
    // Asynchronous: a missing or busy tray must not hold up typing.
    QDBusConnection::sessionBus().asyncCall(msg);
}

void TrayIcon::setVietMode(bool viet) {
    if (m_vietMode == viet) return;
    m_vietMode = viet;
    if (m_itemAdaptor) emit m_itemAdaptor->NewIcon();
    if (m_menuAdaptor) {
        emit m_menuAdaptor->ItemsPropertiesUpdated(
            {{ToggleItem, filtered(itemProperties(ToggleItem), {"toggle-state"})}}, {});
    }
}

bool TrayIcon::hasItem(int id) const {
    return id >= RootItem && id <= QuitItem;
}

QList<int> TrayIcon::childIds(int id) const {
    if (id != RootItem) return {};
    return {ToggleItem, SettingsItem, SeparatorItem, QuitItem};
}

QVariantMap TrayIcon::itemProperties(int id) const {
    switch (id) {
    case RootItem:
        return {{"children-display", "submenu"}};
    case ToggleItem:
        return {{"label", QString::fromUtf8("Gõ tiếng Việt")},
                {"toggle-type", "checkmark"},
                {"toggle-state", m_vietMode ? 1 : 0}};
    case SettingsItem:
        return {{"label", QString::fromUtf8("Bảng điều khiển... (Ctrl+Shift+F5)")},
                {"icon-name", "configure"}};
    case SeparatorItem:
        return {{"type", "separator"}};
    case QuitItem:
        return {{"label", QString::fromUtf8("Thoát bộ gõ")},
                {"icon-name", "application-exit"}};
    }
    return {};
}

void TrayIcon::triggerItem(int id) {
    // Run after the D-Bus reply: quitting inside the call would drop it.
    std::function<void()> action;
    if (id == ToggleItem) action = m_actions.toggleMode;
    else if (id == SettingsItem) action = m_actions.showSettings;
    else if (id == QuitItem) action = m_actions.quit;
    if (action) QTimer::singleShot(0, this, action);
}
