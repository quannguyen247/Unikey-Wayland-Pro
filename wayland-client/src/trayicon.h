#ifndef TRAYICON_H
#define TRAYICON_H

#include <QDBusAbstractAdaptor>
#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <functional>

// com.canonical.dbusmenu types, as the Plasma tray reads them.
struct DBusMenuItem {
    int id = 0;
    QVariantMap properties;
};
struct DBusMenuItemKeys {
    int id = 0;
    QStringList properties;
};
struct DBusMenuLayoutItem {
    int id = 0;
    QVariantMap properties;
    QList<DBusMenuLayoutItem> children;
};
struct DBusMenuEvent {
    int id = 0;
    QString eventId;
    QDBusVariant data;
    uint timestamp = 0;
};
using DBusMenuItemList = QList<DBusMenuItem>;
using DBusMenuItemKeysList = QList<DBusMenuItemKeys>;
using DBusMenuEventList = QList<DBusMenuEvent>;

Q_DECLARE_METATYPE(DBusMenuItem)
Q_DECLARE_METATYPE(DBusMenuItemKeys)
Q_DECLARE_METATYPE(DBusMenuLayoutItem)
Q_DECLARE_METATYPE(DBusMenuEvent)

QDBusArgument& operator<<(QDBusArgument& argument, const DBusMenuItem& item);
const QDBusArgument& operator>>(const QDBusArgument& argument, DBusMenuItem& item);
QDBusArgument& operator<<(QDBusArgument& argument, const DBusMenuItemKeys& item);
const QDBusArgument& operator>>(const QDBusArgument& argument, DBusMenuItemKeys& item);
QDBusArgument& operator<<(QDBusArgument& argument, const DBusMenuLayoutItem& item);
const QDBusArgument& operator>>(const QDBusArgument& argument, DBusMenuLayoutItem& item);
QDBusArgument& operator<<(QDBusArgument& argument, const DBusMenuEvent& event);
const QDBusArgument& operator>>(const QDBusArgument& argument, DBusMenuEvent& event);

class TrayIcon;

class StatusNotifierItemAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierItem")
    Q_PROPERTY(QString Category READ category)
    Q_PROPERTY(QString Id READ id)
    Q_PROPERTY(QString Title READ title)
    Q_PROPERTY(QString Status READ status)
    Q_PROPERTY(QString IconName READ iconName)
    Q_PROPERTY(QString IconThemePath READ iconThemePath)
    Q_PROPERTY(bool ItemIsMenu READ itemIsMenu)
    Q_PROPERTY(QDBusObjectPath Menu READ menu)

public:
    explicit StatusNotifierItemAdaptor(TrayIcon* parent);
    QString category() const { return "ApplicationStatus"; }
    QString id() const { return "unikey-wayland"; }
    QString title() const { return "UniKey"; }
    QString status() const { return "Active"; }
    QString iconName() const;
    QString iconThemePath() const;
    bool itemIsMenu() const { return false; }
    QDBusObjectPath menu() const { return QDBusObjectPath("/MenuBar"); }

public slots:
    void ContextMenu(int x, int y);
    void Activate(int x, int y);
    void SecondaryActivate(int x, int y);
    void Scroll(int delta, const QString& orientation) { Q_UNUSED(delta); Q_UNUSED(orientation); }

signals:
    void NewTitle();
    void NewIcon();
    void NewStatus(const QString& status);
};

class DBusMenuAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.canonical.dbusmenu")
    Q_PROPERTY(uint Version READ version)
    Q_PROPERTY(QString TextDirection READ textDirection)
    Q_PROPERTY(QString Status READ status)
    Q_PROPERTY(QStringList IconThemePath READ iconThemePath)

public:
    DBusMenuAdaptor(QObject* object, TrayIcon* tray);
    uint version() const { return 3; }
    QString textDirection() const { return "ltr"; }
    QString status() const { return "normal"; }
    QStringList iconThemePath() const { return {}; }

public slots:
    uint GetLayout(int parentId, int recursionDepth, const QStringList& propertyNames,
                   DBusMenuLayoutItem& layout);
    DBusMenuItemList GetGroupProperties(const QList<int>& ids, const QStringList& propertyNames);
    QDBusVariant GetProperty(int id, const QString& name);
    void Event(int id, const QString& eventId, const QDBusVariant& data, uint timestamp);
    QList<int> EventGroup(const DBusMenuEventList& events);
    bool AboutToShow(int id);
    QList<int> AboutToShowGroup(const QList<int>& ids, QList<int>& idErrors);

signals:
    void ItemsPropertiesUpdated(const DBusMenuItemList& updatedProps,
                                const DBusMenuItemKeysList& removedProps);
    void LayoutUpdated(uint revision, int parent);
    void ItemActivationRequested(int id, uint timestamp);

private:
    TrayIcon* m_tray;
};

// A StatusNotifierItem with a small dbusmenu, using only QtDBus so the input
// method does not have to load a widget toolkit to show its tray icon.
class TrayIcon : public QObject {
    Q_OBJECT

public:
    struct Actions {
        std::function<void()> toggleMode;
        std::function<void()> showSettings;
        std::function<void()> quit;
    };

    TrayIcon(bool vietMode, Actions actions, QObject* parent = nullptr);

    bool isVietMode() const { return m_vietMode; }
    void setVietMode(bool viet);
    void toggleMode() { if (m_actions.toggleMode) m_actions.toggleMode(); }
    void showSettings() { if (m_actions.showSettings) m_actions.showSettings(); }

    QVariantMap itemProperties(int id) const;
    QList<int> childIds(int id) const;
    bool hasItem(int id) const;
    void triggerItem(int id);

public slots:
    void registerWithWatcher();

private:
    bool m_vietMode;
    Actions m_actions;
    StatusNotifierItemAdaptor* m_itemAdaptor = nullptr;
    DBusMenuAdaptor* m_menuAdaptor = nullptr;
};

#endif // TRAYICON_H
