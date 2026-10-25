#pragma once
#include "PluginManager.h"
#include <QNetworkAccessManager>
#include <QObject>
#include <QVariantList>

// Reads a JSON index of installable extensions and installs them into the
// user's extensions folder. Every download must carry a SHA-256 in the index
// and is verified before it is unpacked. Nothing runs until the user turns the
// extension on.
class Marketplace : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(QString indexUrl READ indexUrl WRITE setIndexUrl NOTIFY indexUrlChanged)
public:
    explicit Marketplace(PluginManager* plugins, QObject* parent = nullptr);
    QVariantList items() const { return m_items; }
    bool busy() const { return m_busy; }
    QString message() const { return m_message; }
    QString indexUrl() const { return m_url; }
    void setIndexUrl(const QString& u);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void install(const QString& id);

signals:
    void itemsChanged();
    void busyChanged();
    void messageChanged();
    void indexUrlChanged();

private:
    void fail(const QString& msg);
    void setBusy(bool b);
    QNetworkAccessManager* net();
    bool unpack(const QByteArray& data, const QVariantMap& item, QString* error);

    PluginManager* m_plugins;
    QNetworkAccessManager* m_net = nullptr;
    QVariantList m_items;
    QString m_url, m_message;
    bool m_busy = false;
};
