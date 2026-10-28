#pragma once
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <functional>
#include <memory>

// Extensions are separate processes that speak newline-delimited JSON-RPC over
// stdin/stdout. They are started on demand, killed after 60 s idle and watched
// for memory use (PSS) so an extension can't blow the app's RAM budget.
// See docs/PLUGINS.md for the protocol.
struct PluginInfo {
    QString id, name, version, description, dir, setup;
    QStringList exec, capabilities;
    bool official = false;
    bool enabled = false;
    int memLimitMB = 20;
    QVariantList options;      // user-editable settings declared in plugin.json (see docs/PLUGINS.md)
};

using PluginReply = std::function<void(bool ok, const QJsonValue& result, const QString& error)>;
using PluginPartial = std::function<void(const QJsonValue& result)>;

class PluginProcess : public QObject {
    Q_OBJECT
public:
    PluginProcess(const PluginInfo& info, QObject* parent = nullptr);
    ~PluginProcess() override;
    void call(const QString& method, const QJsonObject& params, PluginReply cb, int timeoutMs, PluginPartial partial = {});
    void stop(const QString& reason = {});
    bool running() const { return m_proc.state() != QProcess::NotRunning; }
    qint64 pssKB() const { return m_pssKB; }
    QString status() const { return m_status; }
signals:
    void statusChanged();
private:
    struct Pending { PluginReply cb; PluginPartial partial; QTimer* timer; };
    void startIfNeeded();
    void onReadyRead();
    void failAll(const QString& why);
    void finish(int id, bool ok, const QJsonValue& v, const QString& err);
    void checkMemory();

    PluginInfo m_info;
    QProcess m_proc;
    QByteArray m_buf;
    QHash<int, Pending> m_pending;
    QByteArrayList m_outbox;
    QTimer m_idle, m_mem;
    int m_nextId = 1;
    qint64 m_pssKB = 0;
    QString m_status = QStringLiteral("Idle");
    QString m_lastErr;
    bool m_started = false;
};

class PluginManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList list READ list NOTIFY changed)
    Q_PROPERTY(QString userDir READ userDir CONSTANT)
    Q_PROPERTY(bool onboardingPending READ onboardingPending CONSTANT)
public:
    explicit PluginManager(QObject* parent = nullptr);
    QVariantList list() const;
    QString userDir() const { return m_userDir; }
    bool onboardingPending() const;

    Q_INVOKABLE void rescan();
    Q_INVOKABLE void setEnabled(const QString& id, bool on);
    Q_INVOKABLE void move(const QString& id, int delta);
    Q_INVOKABLE void openUserDir();
    Q_INVOKABLE QString installOfficialExtensions();
    Q_INVOKABLE void finishOnboarding();
    Q_INVOKABLE bool uninstall(const QString& id);     // community extensions only
    Q_INVOKABLE void setOption(const QString& id, const QString& key, const QString& value);
    QString optionValue(const QString& id, const QString& key) const;
    Q_INVOKABLE void search(const QString& query);      // source plugins -> searchResult()

    QStringList enabledWith(const QString& capability) const;
    QString nameOf(const QString& id) const;
    void call(const QString& id, const QString& method, const QJsonObject& params, PluginReply cb, int timeoutMs = 15000, PluginPartial partial = {});

signals:
    void changed();
    void searchStarted(int gen, const QStringList& pluginIds);
    void searchResult(int gen, const QString& pluginId, const QString& pluginName, const QVariantList& items, const QString& error);

private:
    QVariantList optionsWithValues(const PluginInfo& i) const;
    void saveOrder();
    QHash<QString, PluginInfo> m_info;
    QStringList m_order;
    QHash<QString, PluginProcess*> m_proc;
    QString m_userDir;
    int m_searchGen = 0;
};
