#include "AudioEngine.h"
#include "Covers.h"
#include "Icons.h"
#include "Library.h"
#include "Lyrics.h"
#include "Palette.h"
#include "Player.h"
#include "PluginManager.h"
#include "UiState.h"
#include "WavyProgress.h"
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <qqml.h>
extern "C" {
#include <libavutil/log.h>
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
    Player player(&engine, &library, &plugins);
    Palette palette;
    UiState state;

    QObject::connect(&player, &Player::coverForPalette, &palette, &Palette::updateFromCover);
    QObject::connect(&player, &Player::paletteReset, &palette, &Palette::reset);
    QObject::connect(&player, &Player::trackInfo, &lyrics, &Lyrics::request);
    QObject::connect(&player, &Player::positionChanged, &lyrics, [&] { lyrics.setPosition(player.position()); });
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &player, &Player::saveSession);

    qmlRegisterType<WavyProgress>("Lumen", 1, 0, "WavyProgress");
    QQmlApplicationEngine qml;
    qml.addImageProvider("cover", new CoverProvider);
    qml.addImageProvider("icon", new IconProvider);
    QQmlContext* ctx = qml.rootContext();
    ctx->setContextProperty("Library", &library);
    ctx->setContextProperty("Player", &player);
    ctx->setContextProperty("Plugins", &plugins);
    ctx->setContextProperty("Lyrics", &lyrics);
    ctx->setContextProperty("Pal", &palette);
    ctx->setContextProperty("Ui", &state);
    qml.load(QUrl("qrc:/qml/Main.qml"));
    if (qml.rootObjects().isEmpty()) return 1;

    player.restoreSession();       // paused, where you left off
    for (const QString& a : app.arguments().mid(1)) library.addFolder(a);

    // Test hook: LUMEN_SHOT=/path.png saves a screenshot and exits.
    const QByteArray shot = qgetenv("LUMEN_SHOT");
    if (!shot.isEmpty()) {
        auto* root = qml.rootObjects().first();
        QTimer::singleShot(1500, [&] {
            if (qEnvironmentVariableIsSet("LUMEN_PAGE")) root->setProperty("page", qEnvironmentVariableIntValue("LUMEN_PAGE"));
            if (qEnvironmentVariableIsSet("LUMEN_ALBUM")) root->setProperty("openAlbum", qEnvironmentVariableIntValue("LUMEN_ALBUM"));
            if (qEnvironmentVariableIsSet("LUMEN_QUERY")) root->setProperty("searchSeed", qEnvironmentVariable("LUMEN_QUERY"));
        });
        QTimer::singleShot(2200, [&] { if (qEnvironmentVariableIntValue("LUMEN_PLAY")) player.playAlbum(qEnvironmentVariableIntValue("LUMEN_PLAYALBUM"), 0, false); });
        if (qEnvironmentVariableIsSet("LUMEN_REMOTE")) QTimer::singleShot(2200, [&] {
            const QString spec = qEnvironmentVariable("LUMEN_REMOTE");      // pluginId:path
            player.playRemote(spec.section(':', 0, 0), QVariantMap{{"id", spec.section(':', 1)}, {"title", "Remote test"}, {"artist", "Extension"}, {"durationMs", 40000}});
        });
        if (qEnvironmentVariableIsSet("LUMEN_SEEK")) QTimer::singleShot(3500, [&] { player.seek(qEnvironmentVariableIntValue("LUMEN_SEEK")); });
        QTimer::singleShot(qEnvironmentVariableIntValue("LUMEN_DELAY") ? qEnvironmentVariableIntValue("LUMEN_DELAY") : 5000, [&] {
            QFile st("/proc/self/status"); st.open(QIODevice::ReadOnly);
            for (const QByteArray& l : st.readAll().split('\n')) if (l.startsWith("VmRSS") || l.startsWith("RssAnon")) qInfo("%s", l.constData());
            qInfo("library=%d folders=%lld", library.trackCount(), (long long)library.folders().size());
            qInfo("now: %s | %lld ms | playing=%d | lyrics=%s lines=%lld cur=%d src=%s", qPrintable(player.title()), (long long)player.position(),
                  int(player.playing()), qPrintable(lyrics.state()), (long long)lyrics.lines().size(), lyrics.current(), qPrintable(lyrics.source()));
            auto* w = qobject_cast<QQuickWindow*>(root);
            w->grabWindow().save(QString::fromLocal8Bit(shot));
            QCoreApplication::quit();
        });
    }
    return app.exec();
}
