#pragma once
#include <QObject>
#include <QVariant>

// Tiny event bus so any QML component can navigate or open menus/dialogs
// without knowing about Main.qml's ids.
class Bus : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentArg READ currentArg WRITE setCurrentArg NOTIFY currentArgChanged)
public:
    QString currentArg() const { return m_arg; }
    void setCurrentArg(const QString& a) { if (a != m_arg) { m_arg = a; emit currentArgChanged(); } }
    using QObject::QObject;
    Q_INVOKABLE void go(const QString& page, const QVariant& arg = {}) { emit navigate(page, arg); }
    Q_INVOKABLE void back() { emit navigateBack(); }
    Q_INVOKABLE void forward() { emit navigateForward(); }
    Q_INVOKABLE void toast(const QString& msg) { emit toastRequested(msg); }
    Q_INVOKABLE void menu(const QString& kind, const QVariantMap& ctx, double x, double y) { emit menuRequested(kind, ctx, x, y); }
    // play something from the online catalog that may not be loaded yet: kind = album | artist | radio
    Q_INVOKABLE void playOnline(const QString& kind, const QString& id) { emit playOnlineRequested(kind, id); }
    Q_INVOKABLE void pickPlaylist(const QStringList& paths) { emit pickRequested(paths); }
    Q_INVOKABLE void prompt(const QString& title, const QString& initial, const QString& action, const QVariant& arg = {}) { emit promptRequested(title, initial, action, arg); }
signals:
    void currentArgChanged();
    void navigate(const QString& page, const QVariant& arg);
    void navigateBack();
    void navigateForward();
    void toastRequested(const QString& msg);
    void menuRequested(const QString& kind, const QVariantMap& ctx, double x, double y);
    void playOnlineRequested(const QString& kind, const QString& id);
    void pickRequested(const QStringList& paths);
    void promptRequested(const QString& title, const QString& initial, const QString& action, const QVariant& arg);
private:
    QString m_arg;
};
