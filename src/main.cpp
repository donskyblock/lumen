#include "AudioEngine.h"
#include "Bus.h"
#include "Catalog.h"
#include "Covers.h"
#include "Icons.h"
#include "Library.h"
#include "Lyrics.h"
#include "Marketplace.h"
#include "Metadata.h"
#include "Palette.h"
#include "Player.h"
#include "Presence.h"
#include "SongIndex.h"
#include "PluginManager.h"
#include "Playlists.h"
#include "Profiles.h"
#include "Resolver.h"
#include "Taste.h"
#include "UiState.h"
#include "WavyProgress.h"
#include <QDir>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QQmlNetworkAccessManagerFactory>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <qqml.h>
extern "C" {
#include <libavutil/log.h>
}

namespace {
// Cover art for online songs is fetched by QML's Image; keep it on disk so it's not re-downloaded.
class CachedNetwork : public QQmlNetworkAccessManagerFactory {
public:
    QNetworkAccessManager* create(QObject* parent) override
    {
        auto* nam = new QNetworkAccessManager(parent);
        auto* cache = new QNetworkDiskCache(nam);
        cache->setCacheDirectory(QDir(Db::get().dataDir()).filePath("imgcache"));
        cache->setMaximumCacheSize(256LL * 1024 * 1024);
        nam->setCache(cache);
        return nam;
    }
};
}

int main(int argc, char** argv)
{
    av_log_set_level(AV_LOG_ERROR);
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("lumen");
    QGuiApplication::setOrganizationName("lumen");
    QGuiApplication::setDesktopFileName("lumen");

    QFont f;
    f.setFamilies({"Inter", "Roboto", "Noto Sans", "Cantarell", "sans-serif"});
    f.setPixelSize(14);
    QGuiApplication::setFont(f);

    qRegisterMetaType<QVector<Track>>();
    qRegisterMetaType<Track>();

    AudioEngine engine;
    Library library;
    PluginManager plugins;
    Lyrics lyrics(&plugins);
    Marketplace market(&plugins);
    Catalog catalog(&library);
    Resolver resolver(&library, &plugins, &catalog);
    Metadata metadata(&catalog);
    Player player(&engine, &library, &plugins);
    player.setResolver(&resolver);
    player.setMetadata(&metadata);
    Presence presence(&player, &plugins);
    SongIndex songIndex(&catalog, &library);
    library.setRemoteHook([&songIndex](const QVector<Track>& t) { songIndex.noteSongs(t, 2); });
    player.setIndex(&songIndex);
    Palette palette;
    Profiles profiles;
    Playlists playlists(&library);
    Taste taste(&library, &catalog, &playlists);
    taste.setPlugins(&plugins);
    taste.setIndex(&songIndex);
    QObject::connect(&player, &Player::autoplayWanted, &taste, &Taste::autoplay);
    QObject::connect(&taste, &Taste::autoplayReady, &player, &Player::autoplayArrived);
    if (qEnvironmentVariableIsSet("LUMEN_LOG_AUTOPLAY"))
        QObject::connect(&taste, &Taste::autoplayReady, &taste, [&](int, const QStringList& p, const QStringList& src) {
            qInfo("autoplay: %lld songs", (long long)p.size());
            for (int i = 0; i < p.size(); ++i) { const auto t = library.trackByPath(p[i]); qInfo("  [%s] %s - %s", qPrintable(src.value(i)), qPrintable(t ? t->artist : p[i]), qPrintable(t ? t->title : QString())); }
        });
    UiState state;
    Bus bus;
    QObject::connect(&player, &Player::problem, &bus, &Bus::toast);
    QObject::connect(&player, &Player::notice, &bus, &Bus::toast);
    if (qEnvironmentVariableIsSet("LUMEN_LOG_NOTICES")) QObject::connect(&player, &Player::notice, &player, [](const QString& m) { qInfo("notice: %s", qPrintable(m)); });
    QObject::connect(&profiles, &Profiles::changed, &palette, [&] { palette.applyProfile(profiles.active()); });
    palette.applyProfile(profiles.active());

    QObject::connect(&player, &Player::coverForPalette, &palette, &Palette::updateFromCover);
    QObject::connect(&player, &Player::paletteReset, &palette, &Palette::reset);
    QObject::connect(&player, &Player::trackInfo, &lyrics, &Lyrics::request);
    QObject::connect(&player, &Player::positionChanged, &lyrics, [&] { lyrics.setPosition(player.position()); });
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &player, &Player::saveSession);
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &presence, &Presence::shutdown);

    qmlRegisterType<WavyProgress>("Lumen", 1, 0, "WavyProgress");
    CachedNetwork netFactory;                      // must outlive the engine (it is destroyed after it)
    QQmlApplicationEngine qml;
    qml.setNetworkAccessManagerFactory(&netFactory);
    qml.addImageProvider("cover", new CoverProvider);
    qml.addImageProvider("icon", new IconProvider);
    QQmlContext* ctx = qml.rootContext();
    ctx->setContextProperty("Library", &library);
    ctx->setContextProperty("Player", &player);
    ctx->setContextProperty("Plugins", &plugins);
    ctx->setContextProperty("Lyrics", &lyrics);
    ctx->setContextProperty("Market", &market);
    ctx->setContextProperty("Pal", &palette);
    ctx->setContextProperty("Profiles", &profiles);
    ctx->setContextProperty("Playlists", &playlists);
    ctx->setContextProperty("Ui", &state);
    ctx->setContextProperty("Catalog", &catalog);
    ctx->setContextProperty("Resolver", &resolver);
    ctx->setContextProperty("Metadata", &metadata);
    ctx->setContextProperty("SongIndex", &songIndex);
    ctx->setContextProperty("Taste", &taste);
    ctx->setContextProperty("Bus", &bus);
    qml.load(QUrl("qrc:/qml/Main.qml"));
    if (qml.rootObjects().isEmpty()) return 1;

    if (qEnvironmentVariableIsSet("LUMEN_MARKET_INSTALL")) {       // test hook
        QObject::connect(&market, &Marketplace::itemsChanged, &market, [&] {
            static bool done = false;
            if (done) return;
            done = true;
            market.install(qEnvironmentVariable("LUMEN_MARKET_INSTALL"));
        });
        QTimer::singleShot(800, &market, &Marketplace::refresh);
    }
    player.restoreSession();       // paused, where you left off
    for (const QString& a : app.arguments().mid(1)) library.addFolder(a);

    // Test hook: LUMEN_SHOT=/path.png saves a screenshot and exits.
    const QByteArray shot = qgetenv("LUMEN_SHOT");
    QObject* const root = qml.rootObjects().first();        // test hooks below run later, so this must outlive the if-block
    if (!shot.isEmpty()) {
        QTimer::singleShot(1500, [&] {
            if (qEnvironmentVariableIsSet("LUMEN_PROFILE")) profiles.create("Test", qEnvironmentVariable("LUMEN_PROFILE"));
                        if (qEnvironmentVariableIsSet("LUMEN_ROUTE")) QMetaObject::invokeMethod(root, "goRoute", Q_ARG(QVariant, qEnvironmentVariable("LUMEN_ROUTE")), Q_ARG(QVariant, qEnvironmentVariable("LUMEN_ROUTE_ARG")));
            if (qEnvironmentVariableIsSet("LUMEN_QUERY")) root->setProperty("query", qEnvironmentVariable("LUMEN_QUERY"));
        });
        QTimer::singleShot(2200, [&] { if (qEnvironmentVariableIntValue("LUMEN_PLAY")) { const auto info = library.home(); (void)info; player.playPaths(library.albumPaths(qEnvironmentVariable("LUMEN_PLAYALBUM")).isEmpty() ? library.albumPaths(library.albumInfo("").value("key").toString()) : library.albumPaths(qEnvironmentVariable("LUMEN_PLAYALBUM")), 0); if (player.title().isEmpty()) { QStringList all; for (const Track& t : library.songsInView()) all << t.path; player.playPaths(all, 0); } } });
        if (qEnvironmentVariableIsSet("LUMEN_REMOTE")) QTimer::singleShot(2200, [&] {
            const QString spec = qEnvironmentVariable("LUMEN_REMOTE");      // pluginId:path
            player.playRemote(spec.section(':', 0, 0), QVariantMap{{"id", spec.section(':', 1)}, {"title", "Remote test"}, {"artist", "Extension"}, {"durationMs", 40000}});
        });
        // catalog test hooks: LUMEN_PLAY_ARTIST / LUMEN_IMPORT_ARTIST = catalog id, LUMEN_RADIO=1, LUMEN_EXPORT / LUMEN_TASTE_IMPORT = file
        if (qEnvironmentVariableIsSet("LUMEN_PLAY_ARTIST")) QTimer::singleShot(1800, [&] {
            const QString id = qEnvironmentVariable("LUMEN_PLAY_ARTIST");
            QObject::connect(&catalog, &Catalog::artistLoaded, &player, [&, id](const QString& got, const QVariantMap&) { if (got == id) player.playPaths(catalog.artistPaths(id).mid(0, qEnvironmentVariableIntValue("LUMEN_QUEUE_LIMIT") > 0 ? qEnvironmentVariableIntValue("LUMEN_QUEUE_LIMIT") : 100000), 0); }, Qt::SingleShotConnection);
            catalog.loadArtist(id);
        });
        if (qEnvironmentVariableIsSet("LUMEN_IMPORT_ARTIST")) QTimer::singleShot(1800, [&] { catalog.importArtist(qEnvironmentVariable("LUMEN_IMPORT_ARTIST")); });
        if (qEnvironmentVariableIsSet("LUMEN_RADIO")) QTimer::singleShot(1800, [&] {
            QObject::connect(&taste, &Taste::radioReady, &player, [&](const QString& t, const QStringList& p) { qInfo("radio '%s': %lld songs", qPrintable(t), (long long)p.size()); for (const QString& x : p.mid(0, 12)) { const auto tr = library.trackByPath(x); qInfo("  %s - %s", qPrintable(tr ? tr->artist : x), qPrintable(tr ? tr->title : QString())); } player.playPaths(p, 0); });
            QObject::connect(&taste, &Taste::radioFailed, &player, [](const QString& w) { qInfo("radio failed: %s", qPrintable(w)); });
            taste.startRadio(qEnvironmentVariable("LUMEN_RADIO"));
        });
        // LUMEN_INDEX_DEMO="id1,id2,id3": pretend those catalog songs were listened to in that order, crawl one artist,
        // then print what the index would offer after the first one
        if (qEnvironmentVariableIsSet("LUMEN_INDEX_DEMO")) QTimer::singleShot(1800, [&] {
            const QStringList ids = qEnvironmentVariable("LUMEN_INDEX_DEMO").split(',', Qt::SkipEmptyParts);
            auto tracks = std::make_shared<QVector<Track>>();
            auto step = std::make_shared<std::function<void(int)>>();
            *step = [&, ids, tracks, step](int i) {
                if (i >= ids.size()) {
                    songIndex.crawlNow();
                    QTimer::singleShot(3000, [&, tracks] {
                        for (const auto& p : songIndex.candidates({tracks->first()}, 14))
                            qInfo("cand: %-22s %.2f  %s - %s", qPrintable(p.why), p.w, qPrintable(p.t.artist), qPrintable(p.t.title));
                    });
                    return;
                }
                catalog.get("/track/" + ids[i], 0, [&, tracks, step, i](const QJsonObject& o, const QString&) {
                    const Track t = catalog.toTrack(o);
                    library.noteRemote({t});
                    songIndex.noteListen(t);
                    tracks->push_back(t);
                    (*step)(i + 1);
                });
            };
            (*step)(0);
        });
        // LUMEN_PLAY_FIXED="id1,id2": play those catalog songs as a playlist (autoplay must not add to it)
        if (qEnvironmentVariableIsSet("LUMEN_PLAY_FIXED")) QTimer::singleShot(1800, [&] {
            const QStringList ids = qEnvironmentVariable("LUMEN_PLAY_FIXED").split(',', Qt::SkipEmptyParts);
            auto paths = std::make_shared<QStringList>();
            auto step = std::make_shared<std::function<void(int)>>();
            *step = [&, ids, paths, step](int i) {
                if (i >= ids.size()) { player.playFixed(*paths, 0); return; }
                catalog.get("/track/" + ids[i], 0, [&, ids, paths, step, i](const QJsonObject& o, const QString&) {
                    const Track t = catalog.toTrack(o);
                    library.noteRemote({t});
                    paths->push_back(t.path);
                    (*step)(i + 1);
                });
            };
            (*step)(0);
        });
        // LUMEN_META_LOOKUP="title|artist" logs what the metadata service finds
        if (qEnvironmentVariableIsSet("LUMEN_META_LOOKUP")) QTimer::singleShot(1800, [&] {
            const QString spec = qEnvironmentVariable("LUMEN_META_LOOKUP");
            metadata.lookup(spec.section('|', 0, 0), spec.section('|', 1, 1), spec.section('|', 2, 2).toLongLong(), [](const SongMeta& m) {
                qInfo("meta: ok=%d conf=%.2f via=%s | %s - %s | album=%s genre=%s year=%d isrc=%s dur=%lld", int(m.ok), m.confidence, qPrintable(m.source),
                      qPrintable(m.artist), qPrintable(m.title), qPrintable(m.album), qPrintable(m.genre), m.year, qPrintable(m.isrc), (long long)m.durationMs);
            });
        });
        // LUMEN_PLAY_ONLINE=<catalog track id> plays it the way a click in "Songs online" does
        if (qEnvironmentVariableIsSet("LUMEN_PLAY_ONLINE")) QTimer::singleShot(1800, [&] {
            const QString path = catalogPath(qEnvironmentVariable("LUMEN_PLAY_ONLINE"));
            catalog.get("/track/" + qEnvironmentVariable("LUMEN_PLAY_ONLINE"), 0, [&, path](const QJsonObject& o, const QString&) {
                library.noteRemote({catalog.toTrack(o)});
                QStringList ctx;
                for (int i = 0; i < 5; ++i) ctx << path;
                player.playOnlineSong(path, ctx);
            });
        });
        // LUMEN_PLAY_EXT=<plugin id>:<item id>|<title>|<artist> plays an extension result like a click in the extension results
        if (qEnvironmentVariableIsSet("LUMEN_PLAY_EXT")) QTimer::singleShot(1800, [&] {
            const QString spec = qEnvironmentVariable("LUMEN_PLAY_EXT");
            const QString pi = spec.section('|', 0, 0);
            player.playRemote(pi.section(':', 0, 0), QVariantMap{{"id", pi.section(':', 1)}, {"title", spec.section('|', 1, 1)}, {"artist", spec.section('|', 2, 2)}, {"durationMs", 181000}});
        });
        if (qEnvironmentVariableIsSet("LUMEN_TASTE_IMPORT")) QTimer::singleShot(1800, [&] {
            QObject::connect(&taste, &Taste::importFinished, &taste, [](const QString& m) { qInfo("import: %s", qPrintable(m)); });
            QObject::connect(&taste, &Taste::importFailed, &taste, [](const QString& m) { qInfo("import failed: %s", qPrintable(m)); });
            taste.importFrom(qEnvironmentVariable("LUMEN_TASTE_IMPORT"));
        });
        if (qEnvironmentVariableIsSet("LUMEN_EXPORT")) QTimer::singleShot(qEnvironmentVariableIntValue("LUMEN_EXPORT_AT") ? qEnvironmentVariableIntValue("LUMEN_EXPORT_AT") : 4500, [&] {
            const QString e = taste.exportTo(qEnvironmentVariable("LUMEN_EXPORT"));
            qInfo("export: %s", e.isEmpty() ? "ok" : qPrintable(e));
        });
        if (qEnvironmentVariableIsSet("LUMEN_SEEK")) QTimer::singleShot(3500, [&] { player.seek(qEnvironmentVariableIntValue("LUMEN_SEEK")); });
        QTimer::singleShot(qEnvironmentVariableIntValue("LUMEN_DELAY") ? qEnvironmentVariableIntValue("LUMEN_DELAY") : 5000, [&] {
#ifndef Q_OS_WIN
            QFile st("/proc/self/status"); st.open(QIODevice::ReadOnly);
            for (const QByteArray& l : st.readAll().split('\n')) if (l.startsWith("VmRSS") || l.startsWith("RssAnon")) qInfo("%s", l.constData());
#endif
            { QStringList ids; for (const QVariant& v : plugins.list()) ids << v.toMap().value("id").toString(); qInfo("extensions=%s | market: %s", qPrintable(ids.join(',')), qPrintable(market.message())); }
            qInfo("library=%d folders=%lld", library.trackCount(), (long long)library.folders().size());
            qInfo("catalog: online=%d saved artists=%lld resolving=%d error='%s' via=%s preview=%d", int(catalog.online()), (long long)Db::get().savedCatArtists().size(), int(player.resolving()), qPrintable(player.error()), qPrintable(player.via()), int(player.isPreview()));
            { const auto st = songIndex.stats(); qInfo("index: songs=%lld artists=%lld links=%lld searches=%lld crawled=%lld", st["songs"].toLongLong(), st["artists"].toLongLong(), st["links"].toLongLong(), st["searches"].toLongLong(), st["crawled"].toLongLong()); }
            qInfo("queue: upnext=%lld first=[%s]", (long long)player.upNext().size(), qPrintable(player.upNext().isEmpty() ? QString() : player.upNext().first().toMap().value("artist").toString() + " - " + player.upNext().first().toMap().value("title").toString()));
            qInfo("now: %s | %lld ms | playing=%d | lyrics=%s lines=%lld cur=%d src=%s", qPrintable(player.title()), (long long)player.position(),
                  int(player.playing()), qPrintable(lyrics.state()), (long long)lyrics.lines().size(), lyrics.current(), qPrintable(lyrics.source()));
            auto* w = qobject_cast<QQuickWindow*>(root);
            w->grabWindow().save(QString::fromLocal8Bit(shot));
            QCoreApplication::quit();
        });
    }
    songIndex.start();
    return app.exec();
}
